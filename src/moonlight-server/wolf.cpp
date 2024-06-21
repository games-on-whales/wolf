#include <boost/asio.hpp>
#include <chrono>
#include <control/control.hpp>
#include <core/docker.hpp>
#include <csignal>
#include <exceptions/exceptions.h>
#include <filesystem>
#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>
#include <memory>
#include <platforms/hw.hpp>
#include <rest/rest.hpp>
#include <rtsp/net.hpp>
#include <state/config.hpp>
#include <streaming/streaming.hpp>
#include <vector>

namespace ba = boost::asio;
namespace fs = std::filesystem;

using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace control;
using namespace wolf::core;

static constexpr int DEFAULT_SESSION_TIMEOUT_MILLIS = 4000;

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

  std::optional<std::string> internal_ip = std::nullopt;
  if (auto override_ip = utils::get_env("WOLF_INTERNAL_IP")) {
    internal_ip = override_ip;
  }
  std::optional<std::string> mac_address = std::nullopt;
  if (auto override_mac = utils::get_env("WOLF_INTERNAL_MAC")) {
    mac_address = override_mac;
  }

  return {getDisplayModes(), getAudioModes(), server_cert, server_pkey, internal_ip, mac_address};
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
    docker::DockerAPI docker_api(utils::get_env("WOLF_DOCKER_SOCKET", "/var/run/docker.sock"));
    auto pulse_socket = fmt::format("{}/pulse-socket", runtime_dir);

    /* Cleanup old leftovers, Pulse will fail to start otherwise */
    std::filesystem::remove(pulse_socket);
    std::filesystem::remove_all(fmt::format("{}/pulse", runtime_dir));

    auto container = docker_api.create(
        docker::Container{
            .id = "",
            .name = "WolfPulseAudio",
            .image = utils::get_env("WOLF_PULSE_IMAGE", "ghcr.io/games-on-whales/pulseaudio:master"),
            .status = docker::CREATED,
            .ports = {},
            .mounts = {docker::MountPoint{.source = runtime_dir, .destination = "/tmp/pulse/", .mode = "rw"}},
            .env = {"XDG_RUNTIME_DIR=/tmp/pulse/", "UNAME=retro", "UID=1000", "GID=1000"}},
        // The following is needed when using podman (or any container that uses SELINUX). This way we can access the
        // socket that is created by PulseAudio from other containers (including this one).
        R"({
                  "HostConfig" : {
                    "SecurityOpt" : ["label=disable"]
                  }
            })");
    if (container && docker_api.start_by_id(container.value().id)) {
      std::this_thread::sleep_for(1000ms); // TODO: configurable? Better way of knowing when ready?
      return {{.server = audio::connect(fmt::format("{}/pulse-socket", runtime_dir)), .container = container}};
    }
  }

  logs::log(logs::warning, "Failed to connect to any PulseAudio server, audio will not be available!");

  return {};
}

using session_devices = immer::map<std::size_t /* session_id */, std::shared_ptr<state::devices_atom_queue>>;

auto setup_sessions_handlers(const immer::box<state::AppState> &app_state,
                             const std::string &runtime_dir,
                             const std::optional<AudioServer> &audio_server) {
  immer::vector_transient<immer::box<dp::handler_registration>> handlers;

  auto wayland_sessions = std::make_shared<
      immer::atom<immer::map<std::size_t /* session_id */, boost::shared_future<virtual_display::wl_state_ptr>>>>();

  /*
   * A queue of devices that are waiting to be plugged, mapped by session_id
   * This way we can accumulate devices here until the docker container is up and running
   */
  auto plugged_devices_queue = std::make_shared<immer::atom<session_devices>>();

  handlers.push_back(app_state->event_bus->register_handler<immer::box<StopStreamEvent>>(
      [&app_state, wayland_sessions, plugged_devices_queue](const immer::box<StopStreamEvent> &ev) {
        // Remove session from app state so that HTTP/S applist gets updated
        app_state->running_sessions->update([&ev](const immer::vector<state::StreamSession> &ses_v) {
          return remove_session(ses_v, {.session_id = ev->session_id});
        });

        // On termination cleanup the WaylandSession; since this is the only reference to it
        // this will effectively destroy the virtual Wayland session
        logs::log(logs::debug, "Deleting WaylandSession {}", ev->session_id);
        wayland_sessions->update([=](const auto map) { return map.erase(ev->session_id); });
        plugged_devices_queue->update([=](const auto map) { return map.erase(ev->session_id); });
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::PlugDeviceEvent>>(
      [plugged_devices_queue](const immer::box<state::PlugDeviceEvent> &hotplug_ev) {
        plugged_devices_queue->update([=](const session_devices map) {
          logs::log(logs::debug, "{} received hot-plug device event", hotplug_ev->session_id);

          if (auto session_devices_queue = map.find(hotplug_ev->session_id)) {
            session_devices_queue->get()->update([=](const auto queue) { return queue.push_back(hotplug_ev); });
          } else {
            logs::log(logs::warning, "Unable to find plugged_devices_queue for session {}", hotplug_ev->session_id);
          }

          return map;
        });
      }));

  // Run process and our custom wayland as soon as a new StreamSession is created
  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::StreamSession>>(
      [=](const immer::box<state::StreamSession> &session) {
        auto wl_promise = std::make_shared<boost::promise<virtual_display::wl_state_ptr>>();
        if (session->app->start_virtual_compositor) {
          wayland_sessions->update([=](const auto map) {
            return map.set(session->session_id,
                           boost::shared_future<virtual_display::wl_state_ptr>(wl_promise->get_future()));
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
          auto all_devices = immer::array_transient<std::string>();

          /* Setup mounted paths */
          immer::array_transient<std::pair<std::string, std::string>> mounted_paths;

          /* Setup environment paths */
          immer::map_transient<std::string, std::string> full_env;
          full_env.set("XDG_RUNTIME_DIR", runtime_dir);

          auto audio_server_name = audio_server ? audio::get_server_name(audio_server->server) : "";
          full_env.set("PULSE_SINK", pulse_sink_name);
          full_env.set("PULSE_SOURCE", pulse_sink_name + ".monitor");
          full_env.set("PULSE_SERVER", audio_server_name);
          mounted_paths.push_back({audio_server_name, audio_server_name});

          auto render_node = session->app->render_node;

          /* Create video virtual wayland compositor */
          if (session->app->start_virtual_compositor) {
            logs::log(logs::debug, "[STREAM_SESSION] Create wayland compositor");

            auto mouse_nodes = session->mouse->get_nodes();
            auto kb_nodes = session->keyboard->get_nodes();
            auto input_nodes = immer::array_transient<std::string>();
            std::copy(mouse_nodes.begin(), mouse_nodes.end(), std::back_inserter(input_nodes));
            std::copy(kb_nodes.begin(), kb_nodes.end(), std::back_inserter(input_nodes));

            auto wl_state = virtual_display::create_wayland_display(input_nodes.persistent(), render_node);
            virtual_display::set_resolution(
                *wl_state,
                {session->display_mode.width, session->display_mode.height, session->display_mode.refreshRate});
            full_env.set("GAMESCOPE_WIDTH", std::to_string(session->display_mode.width));
            full_env.set("GAMESCOPE_HEIGHT", std::to_string(session->display_mode.height));
            full_env.set("GAMESCOPE_REFRESH", std::to_string(session->display_mode.refreshRate));

            /* Setup additional devices paths */
            auto graphic_devices = virtual_display::get_devices(*wl_state);
            std::copy(graphic_devices.begin(), graphic_devices.end(), std::back_inserter(all_devices));

            /* Setup additional env paths */
            for (const auto &env : virtual_display::get_env(*wl_state)) {
              auto split = utils::split(env, '=');

              if (split[0] == "WAYLAND_DISPLAY") {
                auto socket_path = fmt::format("{}/{}", runtime_dir, split[1]);
                logs::log(logs::debug, "WAYLAND_DISPLAY={}", socket_path);
                mounted_paths.push_back({socket_path, socket_path});
              }

              full_env.set(utils::to_string(split[0]), utils::to_string(split[1]));
            }

            // Set the wayland display
            session->wayland_display->store(wl_state);
            wl_promise->set_value(std::move(wl_state));
          }

          /* Adding custom state folder */
          mounted_paths.push_back({session->app_state_folder, "/home/retro"});

          /* Additional GPU devices */
          auto additional_devices = linked_devices(render_node);
          std::copy(additional_devices.begin(), additional_devices.end(), std::back_inserter(all_devices));

          /* nvidia needs some extra paths */
          if (get_vendor(render_node) == NVIDIA) {
            if (auto driver_volume = utils::get_env("NVIDIA_DRIVER_VOLUME_NAME")) {
              logs::log(logs::info, "Mounting nvidia driver {}:/usr/nvidia", driver_volume);
              mounted_paths.push_back({driver_volume, "/usr/nvidia"});
            } else {
              logs::log(logs::info, "NVIDIA_DRIVER_VOLUME_NAME not set, assuming nvidia driver toolkit is installed..");
            }
          }

          if (get_vendor(render_node) == INTEL) {
            full_env.set("INTEL_DEBUG", "norbc"); // see: https://github.com/games-on-whales/wolf/issues/50
          }

          /* Initialise plugged device queue with mouse and keyboard */
          plugged_devices_queue->update([=](const session_devices map) {
            auto devices = immer::vector<immer::box<state::PlugDeviceEvent>>{
                state::PlugDeviceEvent{.session_id = session->session_id,
                                       .udev_events = session->mouse->get_udev_events(),
                                       .udev_hw_db_entries = session->mouse->get_udev_hw_db_entries()},
                state::PlugDeviceEvent{.session_id = session->session_id,
                                       .udev_events = session->keyboard->get_udev_events(),
                                       .udev_hw_db_entries = session->keyboard->get_udev_hw_db_entries()}};
            /* Update (or create) the queue with the plugged mouse and keyboard */
            if (auto session_devices_queue = map.find(session->session_id)) {
              session_devices_queue->get()->update([=](const auto queue) {
                immer::vector_transient<immer::box<state::PlugDeviceEvent>> new_queue = queue.transient();
                for (const auto device : devices) {
                  new_queue.push_back(device);
                }
                return new_queue.persistent();
              });
              return map;
            } else {
              return map.set(session->session_id, std::make_shared<state::devices_atom_queue>(devices));
            }
          });
          std::shared_ptr<state::devices_atom_queue> session_devices_queue =
              *plugged_devices_queue->load()->find(session->session_id);

          /* Finally run the app, this will stop here until over */
          session->app->runner->run(session->session_id,
                                    session->app_state_folder,
                                    session_devices_queue,
                                    all_devices.persistent(),
                                    mounted_paths.persistent(),
                                    full_env.persistent());

          /* App exited, cleanup */
          logs::log(logs::debug, "[STREAM_SESSION] Remove virtual audio sink");
          if (audio_server && audio_server->server) {
            audio::delete_virtual_sink(audio_server->server, v_device);
          }

          session->wayland_display->store(nullptr);

          /* When the app closes there's no point in keeping the stream running */
          app_state->event_bus->fire_event(
              immer::box<StopStreamEvent>(StopStreamEvent{.session_id = session->session_id}));
        }).detach();
      }));

  // Video streaming pipeline
  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::VideoSession>>(
      [=](const immer::box<state::VideoSession> &sess) {
        std::thread([=]() {
          boost::promise<unsigned short> port_promise;
          auto port_fut = port_promise.get_future();
          std::once_flag called;
          auto ev_handler = app_state->event_bus->register_handler<immer::box<state::RTPVideoPingEvent>>(
              [pp = std::ref(port_promise), &called, sess](const immer::box<state::RTPVideoPingEvent> &ping_ev) {
                std::call_once(called, [=]() { // We'll keep receiving PING requests, but we only want the first one
                  if (ping_ev->client_ip == sess->client_ip) {
                    pp.get().set_value(ping_ev->client_port); // This throws when set multiple times
                  }
                });
              });

          std::shared_ptr<std::atomic_bool> cancel_job = std::make_shared<std::atomic<bool>>(false);
          auto cancel_event = app_state->event_bus->register_handler<immer::box<state::VideoSession>>(
              [=](const immer::box<state::VideoSession> &new_sess) {
                if (new_sess->session_id == sess->session_id) {
                  // A new VideoSession has been queued whilst we still haven't received a PING
                  *cancel_job = true;
                }
              });

          logs::log(logs::debug, "Video session {}, waiting for PING...", sess->session_id);

          // Stop here until we get a PING
          auto status = port_fut.wait_for(boost::chrono::milliseconds(DEFAULT_SESSION_TIMEOUT_MILLIS));
          if (status != boost::future_status::ready) {
            logs::log(logs::warning, "Video session {} timed out waiting for PING", sess->session_id);
            return;
          }
          auto client_port = port_fut.get();
          cancel_event.unregister();
          ev_handler.unregister();

          if (*cancel_job) {
            return;
          }

          virtual_display::wl_state_ptr wl_state;
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
          std::once_flag called;
          auto ev_handler = app_state->event_bus->register_handler<immer::box<state::RTPAudioPingEvent>>(
              [pp = std::ref(port_promise), &called, sess](const immer::box<state::RTPAudioPingEvent> &ping_ev) {
                std::call_once(called, [=]() { // We'll keep receiving PING requests, but we only want the first one
                  if (ping_ev->client_ip == sess->client_ip) {
                    pp.get().set_value(ping_ev->client_port); // This throws when set multiple times
                  }
                });
              });

          std::shared_ptr<std::atomic_bool> cancel_job = std::make_shared<std::atomic<bool>>(false);
          auto cancel_event = app_state->event_bus->register_handler<immer::box<state::AudioSession>>(
              [=](const immer::box<state::AudioSession> &new_sess) {
                if (new_sess->session_id == sess->session_id) {
                  // A new AudioSession has been queued whilst we still haven't received a PING
                  *cancel_job = true;
                }
              });

          logs::log(logs::debug, "Audio session {}, waiting for PING...", sess->session_id);

          // Stop here until we get a PING
          auto status = port_fut.wait_for(boost::chrono::milliseconds(DEFAULT_SESSION_TIMEOUT_MILLIS));
          if (status != boost::future_status::ready) {
            logs::log(logs::warning, "Audio session {} timed out waiting for PING", sess->session_id);
            return;
          }
          auto client_port = port_fut.get();
          cancel_event.unregister();
          ev_handler.unregister();

          if (*cancel_job) {
            return;
          }

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
void run() {
  streaming::init(); // Need to initialise gstreamer once
  control::init();   // Need to initialise enet once
  docker::init();    // Need to initialise libcurl once

  auto runtime_dir = utils::get_env("XDG_RUNTIME_DIR", "/tmp/sockets");
  logs::log(logs::debug, "XDG_RUNTIME_DIR={}", runtime_dir);

  auto config_file = utils::get_env("WOLF_CFG_FILE", "config.toml");
  auto p_key_file = utils::get_env("WOLF_PRIVATE_KEY_FILE", "key.pem");
  auto p_cert_file = utils::get_env("WOLF_PRIVATE_CERT_FILE", "cert.pem");
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

  auto audio_server = setup_audio_server(runtime_dir);
  auto sess_handlers = setup_sessions_handlers(local_state, runtime_dir, audio_server);

  http_thread.join(); // Let's park the main thread over here

  for (const auto &handler : sess_handlers) {
    handler->unregister();
  }
}

int main(int argc, char *argv[]) try {
  logs::init(logs::parse_level(utils::get_env("WOLF_LOG_LEVEL", "INFO")));
  // Exception and termination handling
  std::signal(SIGINT, shutdown_handler);
  std::signal(SIGTERM, shutdown_handler);
  std::signal(SIGQUIT, shutdown_handler);
  std::signal(SIGSEGV, shutdown_handler);
  std::signal(SIGABRT, shutdown_handler);
  std::set_terminate(on_terminate);
  check_exceptions();

  run(); // Main loop
} catch (...) {
  on_terminate();
}