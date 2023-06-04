#include <audio/audio.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <chrono>
#include <control/control.hpp>
#include <csignal>
#include <docker/docker.hpp>
#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>
#include <memory>
#include <rest/rest.hpp>
#include <rtp/udp-ping.hpp>
#include <rtsp/net.hpp>
#include <state/config.hpp>
#include <streaming/streaming.hpp>
#include <vector>

namespace ba = boost::asio;
using namespace std::string_literals;
using namespace std::chrono_literals;

const char *get_env(const char *tag, const char *def = nullptr) noexcept {
  const char *ret = std::getenv(tag);
  return ret ? ret : def;
}

/**
 * @brief Will try to load the config file and fallback to defaults
 */
auto load_config(std::string_view config_file, const std::shared_ptr<dp::event_bus> &ev_bus) {
  logs::log(logs::info, "Reading config file from: {}", config_file);
  return state::load_or_default(config_file.data(), ev_bus);
}

/**
 * @brief Get the Display Modes
 */
immer::array<moonlight::DisplayMode> getDisplayModes() {
  return {{1920, 1080, 60}, {1024, 768, 30}};
}

/**
 * @brief Get the Audio Modes
 */
immer::array<state::AudioMode> getAudioModes() {
  // Stereo
  return {{2, 1, 1, {state::AudioMode::FRONT_LEFT, state::AudioMode::FRONT_RIGHT}}};
}

state::Host get_host_config(std::string_view pkey_filename, std::string_view cert_filename) {
  X509 *server_cert;
  EVP_PKEY *server_pkey;
  if (x509::cert_exists(pkey_filename, cert_filename)) {
    logs::log(logs::debug, "Loading server certificates from disk: {} {}", cert_filename, pkey_filename);
    server_cert = x509::cert_from_file(cert_filename);
    server_pkey = x509::pkey_from_file(pkey_filename);
  } else {
    logs::log(logs::info, "x509 certificates not present, generating: {} {}", cert_filename, pkey_filename);
    server_pkey = x509::generate_key();
    server_cert = x509::generate_x509(server_pkey);
    x509::write_to_disk(server_pkey, pkey_filename, server_cert, cert_filename);
  }

  // TODO: get network info from the host
  auto external_ip = "";
  auto internal_ip = "";
  auto mac_address = "";

  return {getDisplayModes(), getAudioModes(), server_cert, server_pkey, external_ip, internal_ip, mac_address};
}

/**
 * @brief Local state initialization
 */
auto initialize(std::string_view config_file, std::string_view pkey_filename, std::string_view cert_filename) {
  auto event_bus = std::make_shared<dp::event_bus>();
  auto config = load_config(config_file, event_bus);
  auto display_modes = getDisplayModes();

  auto host = get_host_config(pkey_filename, cert_filename);
  auto state = state::AppState{
      .config = config,
      .host = host,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .event_bus = event_bus,
      .running_sessions = std::make_shared<immer::atom<immer::vector<state::StreamSession>>>()};
  return immer::box<state::AppState>(state);
}

static std::string backtrace_file_src() {
  return fmt::format("{}/backtrace.dump", get_env("WOLF_CFG_FOLDER", "."));
}

static void shutdown_handler(int signum) {
  logs::log(logs::info, "Received interrupt signal {}, clean exit", signum);
  if (signum == SIGABRT || signum == SIGSEGV) {
    logs::log(logs::error, "Runtime error, dumping stacktrace to {}", backtrace_file_src());
    boost::stacktrace::safe_dump_to(backtrace_file_src().c_str());
  }

  logs::log(logs::info, "See ya!");
  exit(signum);
}

/**
 * @brief: if an exception was raised we should have created a dump file, here we can pretty print it
 */
void check_exceptions() {
  if (boost::filesystem::exists(backtrace_file_src())) {
    std::ifstream ifs(backtrace_file_src());

    auto st = boost::stacktrace::stacktrace::from_dump(ifs);
    logs::log(logs::error, "Previous run crashed: \n{}", to_string(st));

    // cleaning up
    ifs.close();
    auto now = std::chrono::system_clock::now();
    boost::filesystem::rename(
        backtrace_file_src(),
        fmt::format("{}/backtrace.{:%Y-%m-%d-%H-%M-%S}.dump", get_env("WOLF_CFG_FOLDER", "."), now));
  }
}

struct AudioServer {
  std::shared_ptr<audio::Server> server;
  std::optional<docker::Container> container = {};
};

/**
 * We first try to connect to a running PulseAudio server
 * if that fails, we run our own PulseAudio container and connect to it
 * if that fails, we can't return an AudioServer, hence the optional!
 */
std::optional<AudioServer> setup_audio_server(const std::string &runtime_dir) {
  auto audio_server = audio::connect();
  if (audio::connected(audio_server)) {
    return {{.server = audio_server}};
  } else {
    logs::log(logs::info, "Starting PulseAudio docker container");
    docker::DockerAPI docker_api(get_env("WOLF_DOCKER_SOCKET", "/var/run/docker.sock"));
    auto container = docker_api.create(docker::Container{
        .id = "",
        .name = "WolfPulseAudio",
        .image = get_env("WOLF_PULSE_IMAGE", "ghcr.io/games-on-whales/pulseaudio:master"),
        .status = docker::CREATED,
        .ports = {},
        .mounts = {docker::MountPoint{.source = runtime_dir, .destination = "/tmp/pulse/", .mode = "rw"}},
        .env = {{"XDG_RUNTIME_DIR", runtime_dir}}});
    if (container && docker_api.start_by_id(container.value().id)) {
      std::this_thread::sleep_for(1000ms); // TODO: configurable? Better way of knowing when ready?
      return {{.server = audio::connect(fmt::format("{}/pulse-socket", runtime_dir)), .container = container}};
    }
  }

  return {};
}

auto setup_sessions_handlers(const immer::box<state::AppState> &app_state,
                             const std::string &runtime_dir,
                             const std::optional<AudioServer> &audio_server) {
  immer::vector_transient<immer::box<dp::handler_registration>> handlers;

  auto wayland_sessions =
      std::make_shared<immer::atom<immer::map<std::size_t, boost::shared_future<streaming::wl_state_ptr>>>>();

  // On termination cleanup the WaylandSession; since this is the only reference to it
  // this will effectively destroy the virtual Wayland session
  handlers.push_back(app_state->event_bus->register_handler<immer::box<moonlight::StopStreamEvent>>(
      [wayland_sessions](const immer::box<moonlight::StopStreamEvent> &ev) {
        logs::log(logs::debug, "Deleting WaylandSession {}", ev->session_id);
        wayland_sessions->update([=](const auto map) { return map.erase(ev->session_id); });
      }));

  // Run process and our custom wayland as soon as a new StreamSession is created
  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::StreamSession>>(
      [=](const immer::box<state::StreamSession> &session) {
        auto wl_promise = std::make_shared<boost::promise<streaming::wl_state_ptr>>();
        if (session->app->start_virtual_compositor) {
          wayland_sessions->update([=](const auto map) {
            return map.set(session->session_id,
                           boost::shared_future<streaming::wl_state_ptr>(wl_promise->get_future()));
          });
        }
        // Start selected app on a separate thread
        std::thread([=]() {
          /* Create audio virtual sink */
          logs::log(logs::debug, "[STREAM_SESSION] Create virtual audio sink");
          auto pulse_sink_name = fmt::format("virtual_sink_{}", session->session_id);
          std::shared_ptr<audio::VSink> v_device;
          if (audio_server && audio_server->server) {
            v_device = audio::create_virtual_sink(audio_server->server,
                                                  audio::AudioDevice{.sink_name = pulse_sink_name,
                                                                     .n_channels = session->audio_mode.channels,
                                                                     .bitrate = 48000}); // TODO:
          }

          /* Setup devices paths */
          auto full_devices = session->virtual_inputs.devices_paths.transient();

          /* Setup mounted paths */
          immer::array_transient<std::pair<std::string, std::string>> mounted_paths;

          /* Setup environment paths */
          immer::map_transient<std::string, std::string> full_env;
          full_env.set("XDG_RUNTIME_DIR", runtime_dir);

          auto audio_server_name = audio_server ? audio::get_server_name(audio_server->server) : "";
          full_env.set("PULSE_SINK", pulse_sink_name);
          full_env.set("PULSE_SOURCE", pulse_sink_name + ".monitor");

          if (audio_server->container) {
            full_env.set("PULSE_SERVER", audio_server_name);
            mounted_paths.push_back({audio_server_name, audio_server_name});
          }

          /* Create video virtual wayland compositor */
          if (session->app->start_virtual_compositor) {
            logs::log(logs::debug, "[STREAM_SESSION] Create wayland compositor");
            auto wl_state = streaming::create_wayland_display(session->virtual_inputs.devices_paths,
                                                              get_env("WOLF_RENDER_NODE", "/dev/dri/renderD128"));
            streaming::set_resolution(*wl_state, session->display_mode);
            full_env.set("GAMESCOPE_WIDTH", std::to_string(session->display_mode.width));
            full_env.set("GAMESCOPE_HEIGHT", std::to_string(session->display_mode.height));
            full_env.set("GAMESCOPE_REFRESH", std::to_string(session->display_mode.refreshRate));

            /* Setup additional devices paths */
            auto graphic_devices = streaming::get_devices(*wl_state);
            std::copy(graphic_devices.begin(), graphic_devices.end(), std::back_inserter(full_devices));

            /* Setup additional env paths */
            for (const auto &env : streaming::get_env(*wl_state)) {
              auto split = utils::split(env, '=');

              if (split[0] == "WAYLAND_DISPLAY") {
                auto socket_path = fmt::format("{}/{}", runtime_dir, split[1]);
                logs::log(logs::debug, "WAYLAND_DISPLAY={}", socket_path);
                mounted_paths.push_back({socket_path, socket_path});
              }

              full_env.set(utils::to_string(split[0]), utils::to_string(split[1]));
            }

            wl_promise->set_value(std::move(wl_state));
          }

          /* nvidia needs some extra paths */
          if (session->app->h264_encoder == state::NVIDIA || session->app->hevc_encoder == state::NVIDIA) {
            mounted_paths.push_back({get_env("NVIDIA_DRIVER_VOLUME_NAME", "nvidia-driver-vol"), "/usr/nvidia"});
            full_devices.push_back("/dev/nvidia0");
            full_devices.push_back("/dev/nvidia-modeset");
            full_devices.push_back("/dev/nvidia-uvm");
            full_devices.push_back("/dev/nvidia-uvm-tools");
            full_devices.push_back("/dev/nvidiactl");
          }

          /* Finally run the app, this will stop here until over */
          session->app->runner->run(session->session_id,
                                    full_devices.persistent(),
                                    mounted_paths.persistent(),
                                    full_env.persistent());

          /* App exited, cleanup */
          logs::log(logs::debug, "[STREAM_SESSION] Remove virtual audio sink");
          if (audio_server && audio_server->server) {
            audio::delete_virtual_sink(audio_server->server, v_device);
          }

          /* When the app closes there's no point in keeping the stream running */
          app_state->event_bus->fire_event(
              immer::box<moonlight::StopStreamEvent>(moonlight::StopStreamEvent{.session_id = session->session_id}));
        }).detach();
      }));

  // Video streaming pipeline
  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::VideoSession>>(
      [=](const immer::box<state::VideoSession> &sess) {
        std::thread([=]() {
          boost::promise<unsigned short> port_promise;
          auto port_fut = port_promise.get_future();
          auto ev_handler = app_state->event_bus->register_handler<immer::box<state::RTPVideoPingEvent>>(
              [pp = std::ref(port_promise), sess](const immer::box<state::RTPVideoPingEvent> &ping_ev) {
                if (ping_ev->client_ip == sess->client_ip) {
                  pp.get().set_value(ping_ev->client_port);
                }
              });

          auto client_port = port_fut.get();
          ev_handler.unregister(); // We'll keep receiving PING requests, but we only want the first one

          streaming::wl_state_ptr wl_state;
          if (auto wayland_promise = wayland_sessions->load()->find(sess->session_id)) {
            wl_state = wayland_promise->get(); // Stops here until the wayland socket is ready
          }
          streaming::start_streaming_video(sess, app_state->event_bus, std::move(wl_state), client_port);
        }).detach();
      }));

  // Audio streaming pipeline
  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::AudioSession>>(
      [=](const immer::box<state::AudioSession> &sess) {
        std::thread([=]() {
          boost::promise<unsigned short> port_promise;
          auto port_fut = port_promise.get_future();
          auto ev_handler = app_state->event_bus->register_handler<immer::box<state::RTPAudioPingEvent>>(
              [pp = std::ref(port_promise), sess](const immer::box<state::RTPAudioPingEvent> &ping_ev) {
                if (ping_ev->client_ip == sess->client_ip) {
                  pp.get().set_value(ping_ev->client_port);
                }
              });
          auto client_port = port_fut.get();
          ev_handler.unregister(); // We'll keep receiving PING requests, but we only want the first one

          auto audio_server_name = audio_server ? audio::get_server_name(audio_server->server)
                                                : std::optional<std::string>();
          auto sink_name = fmt::format("virtual_sink_{}.monitor", sess->session_id);
          auto server_name = audio_server_name ? audio_server_name.value() : "";

          streaming::start_streaming_audio(sess, app_state->event_bus, client_port, sink_name, server_name);
        }).detach();
      }));

  return handlers.persistent();
}

/**
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::parse_level(get_env("WOLF_LOG_LEVEL", "INFO")));
  // Exception and termination handling
  check_exceptions();
  std::signal(SIGINT, shutdown_handler);
  std::signal(SIGTERM, shutdown_handler);
  std::signal(SIGQUIT, shutdown_handler);
  std::signal(SIGSEGV, shutdown_handler);
  std::signal(SIGABRT, shutdown_handler);

  streaming::init(); // Need to initialise gstreamer once
  control::init();   // Need to initialise enet once
  docker::init();    // Need to initialise libcurl once

  auto runtime_dir = get_env("XDG_RUNTIME_DIR", "/tmp/sockets");
  logs::log(logs::debug, "XDG_RUNTIME_DIR={}", runtime_dir);

  auto config_file = get_env("WOLF_CFG_FILE", "config.toml");
  auto p_key_file = get_env("WOLF_PRIVATE_KEY_FILE", "key.pem");
  auto p_cert_file = get_env("WOLF_PRIVATE_CERT_FILE", "cert.pem");
  auto local_state = initialize(config_file, p_key_file, p_cert_file);

  // HTTP APIs
  auto http_thread = std::thread([local_state]() {
    HttpServer server = HttpServer();
    HTTPServers::startServer(&server, local_state, state::HTTP_PORT);
  });

  // HTTPS APIs
  std::thread([local_state, p_key_file, p_cert_file]() {
    HttpsServer server = HttpsServer(p_cert_file, p_key_file);
    HTTPServers::startServer(&server, local_state, state::HTTPS_PORT);
  }).detach();

  // RTSP
  std::thread([sessions = local_state->running_sessions, ev_bus = local_state->event_bus]() {
    rtsp::run_server(state::RTSP_SETUP_PORT, sessions, ev_bus);
  }).detach();

  // Control
  std::thread([sessions = local_state->running_sessions, ev_bus = local_state->event_bus]() {
    control::run_control(state::CONTROL_PORT, sessions, ev_bus);
  }).detach();

  // Video RTP Ping
  std::thread([local_state]() {
    rtp::wait_for_ping(state::VIDEO_PING_PORT, [=](unsigned short client_port, const std::string &client_ip) {
      logs::log(logs::trace, "[PING] video from {}:{}", client_ip, client_port);
      auto ev = state::RTPVideoPingEvent{.client_ip = client_ip, .client_port = client_port};
      local_state->event_bus->fire_event(immer::box<state::RTPVideoPingEvent>(ev));
    });
  }).detach();

  // Audio RTP Ping
  std::thread([local_state]() {
    rtp::wait_for_ping(state::AUDIO_PING_PORT, [=](unsigned short client_port, const std::string &client_ip) {
      logs::log(logs::trace, "[PING] audio from {}:{}", client_ip, client_port);
      auto ev = state::RTPAudioPingEvent{.client_ip = client_ip, .client_port = client_port};
      local_state->event_bus->fire_event(immer::box<state::RTPAudioPingEvent>(ev));
    });
  }).detach();

  auto audio_server = setup_audio_server(runtime_dir);
  auto sess_handlers = setup_sessions_handlers(local_state, runtime_dir, audio_server);

  http_thread.join(); // Let's park the main thread over here

  for (const auto &handler : sess_handlers) {
    handler->unregister();
  }
}