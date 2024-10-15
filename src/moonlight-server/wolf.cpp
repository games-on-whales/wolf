#include <api/api.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <control/control.hpp>
#include <core/docker.hpp>
#include <core/gstreamer.hpp>
#include <csignal>
#include <exceptions/exceptions.h>
#include <filesystem>
#include <immer/array_transient.hpp>
#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>
#include <mdns_cpp/logger.hpp>
#include <mdns_cpp/mdns.hpp>
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
using namespace wolf::core;

static constexpr int DEFAULT_SESSION_TIMEOUT_MILLIS = 4000;

/**
 * @brief Will try to load the config file and fallback to defaults
 */
auto load_config(std::string_view config_file,
                 const std::shared_ptr<events::EventBusType> &ev_bus,
                 state::SessionsAtoms running_sessions) {
  logs::log(logs::info, "Reading config file from: {}", config_file);
  return state::load_or_default(config_file.data(), ev_bus, running_sessions);
}

state::Host get_host_config(std::string_view pkey_filename, std::string_view cert_filename) {
  x509::x509_ptr server_cert;
  x509::pkey_ptr server_pkey;
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

  return {state::DISPLAY_CONFIGURATIONS,
          state::AUDIO_CONFIGURATIONS,
          server_cert,
          server_pkey,
          internal_ip,
          mac_address};
}

/**
 * @brief Local state initialization
 */
auto initialize(std::string_view config_file, std::string_view pkey_filename, std::string_view cert_filename) {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = load_config(config_file, event_bus, running_sessions);

  auto host = get_host_config(pkey_filename, cert_filename);
  auto state = state::AppState{
      .config = config,
      .host = host,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions};
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
      auto ms = std::stoi(utils::get_env("WOLF_PULSE_CONTAINER_TIMEOUT_MS", "2000"));
      std::this_thread::sleep_for(std::chrono::milliseconds(ms)); // TODO: Better way of knowing when ready?
      return {{.server = audio::connect(fmt::format("{}/pulse-socket", runtime_dir)), .container = container}};
    }
  }

  logs::log(logs::warning, "Failed to connect to any PulseAudio server, audio will not be available!");

  return {};
}

using session_devices = immer::map<std::size_t /* session_id */, std::shared_ptr<events::devices_atom_queue>>;

auto setup_sessions_handlers(const immer::box<state::AppState> &app_state,
                             const std::string &runtime_dir,
                             const std::optional<AudioServer> &audio_server) {
  immer::vector_transient<immer::box<events::EventBusHandlers>> handlers;

  /*
   * A queue of devices that are waiting to be plugged, mapped by session_id
   * This way we can accumulate devices here until the docker container is up and running
   */
  auto plugged_devices_queue = std::make_shared<immer::atom<session_devices>>();

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [&app_state, plugged_devices_queue](const immer::box<events::StopStreamEvent> &ev) {
        // Remove session from app state so that HTTP/S applist gets updated
        // This should effectively destroy the virtual Wayland session since it holds the last reference
        app_state->running_sessions->update([&ev](const immer::vector<events::StreamSession> &ses_v) {
          return state::remove_session(ses_v, {.session_id = ev->session_id});
        });

        plugged_devices_queue->update([=](const auto map) { return map.erase(ev->session_id); });
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::PlugDeviceEvent>>(
      [plugged_devices_queue](const immer::box<events::PlugDeviceEvent> &hotplug_ev) {
        logs::log(logs::debug, "{} received hot-plug device event", hotplug_ev->session_id);

        if (auto session_devices_queue = plugged_devices_queue->load()->find(hotplug_ev->session_id)) {
          session_devices_queue->get()->push(hotplug_ev);
        } else {
          logs::log(logs::warning, "Unable to find plugged_devices_queue for session {}", hotplug_ev->session_id);
        }
      }));

  // Run process and our custom wayland as soon as a new StreamSession is created
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StreamSession>>(
      [=](const immer::box<events::StreamSession> &session) {
        /* Initialise plugged device queue */
        auto devices_q = std::make_shared<events::devices_atom_queue>();
        plugged_devices_queue->update(
            [=](const session_devices map) { return map.set(session->session_id, devices_q); });

        if (session->app->start_virtual_compositor) {
          logs::log(logs::debug, "[STREAM_SESSION] Create wayland compositor");

          auto render_node = session->app->render_node;
          auto wl_state = virtual_display::create_wayland_display({}, render_node);
          virtual_display::set_resolution(
              *wl_state,
              {session->display_mode.width, session->display_mode.height, session->display_mode.refreshRate});

          // Set the wayland display
          session->wayland_display->store(wl_state);

          // Set virtual devices
          session->mouse->emplace(virtual_display::WaylandMouse(wl_state));
          session->keyboard->emplace(virtual_display::WaylandKeyboard(wl_state));

          // Start Gstreamer producer pipeline
          std::thread([session, wl_state]() {
            streaming::start_video_producer(session->session_id,
                                            wl_state,
                                            {.width = session->display_mode.width,
                                             .height = session->display_mode.height,
                                             .refreshRate = session->display_mode.refreshRate},
                                            session->event_bus);
          }).detach();
        } else {
          // Create virtual devices
          auto mouse = input::Mouse::create();
          if (!mouse) {
            logs::log(logs::error, "Failed to create mouse: {}", mouse.getErrorMessage());
          } else {
            auto mouse_ptr = input::Mouse(std::move(*mouse));
            devices_q->push(immer::box<events::PlugDeviceEvent>(
                events::PlugDeviceEvent{.session_id = session->session_id,
                                        .udev_events = mouse_ptr.get_udev_events(),
                                        .udev_hw_db_entries = mouse_ptr.get_udev_hw_db_entries()}));
            session->mouse->emplace(std::move(mouse_ptr));
          }

          auto keyboard = input::Keyboard::create();
          if (!keyboard) {
            logs::log(logs::error, "Failed to create keyboard: {}", keyboard.getErrorMessage());
          } else {
            auto keyboard_ptr = input::Keyboard(std::move(*keyboard));
            devices_q->push(immer::box<events::PlugDeviceEvent>(
                events::PlugDeviceEvent{.session_id = session->session_id,
                                        .udev_events = keyboard_ptr.get_udev_events(),
                                        .udev_hw_db_entries = keyboard_ptr.get_udev_hw_db_entries()}));
            session->keyboard->emplace(std::move(keyboard_ptr));
          }
        }

        /* Create audio virtual sink */
        logs::log(logs::debug, "[STREAM_SESSION] Create virtual audio sink");
        auto pulse_sink_name = fmt::format("virtual_sink_{}", session->session_id);
        std::shared_ptr<audio::VSink> v_device;
        if (session->app->start_audio_server && audio_server && audio_server->server) {
          v_device = audio::create_virtual_sink(
              audio_server->server,
              audio::AudioDevice{.sink_name = pulse_sink_name,
                                 .mode = state::get_audio_mode(session->audio_channel_count, true)});
          session->audio_sink->store(v_device);

          std::thread([session, audio_server = audio_server->server]() {
            auto sink_name = fmt::format("virtual_sink_{}.monitor", session->session_id);
            streaming::start_audio_producer(session->session_id,
                                            session->event_bus,
                                            session->audio_channel_count,
                                            sink_name,
                                            audio::get_server_name(audio_server));
          }).detach();
        }

        session->event_bus->fire_event(immer::box<events::StartRunner>(
            events::StartRunner{.stop_stream_when_over = true,
                                .runner = session->app->runner,
                                .stream_session = std::make_shared<events::StreamSession>(*session)}));
      }));

  /* Start runner */
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StartRunner>>(
      [=](const immer::box<events::StartRunner> &run_session) {
        // Start selected app on a separate thread
        std::thread([=]() {
          auto session = run_session->stream_session;

          /* Setup devices paths */
          auto all_devices = immer::array_transient<std::string>();

          /* Setup mounted paths */
          immer::array_transient<std::pair<std::string, std::string>> mounted_paths;

          /* Setup environment paths */
          immer::map_transient<std::string, std::string> full_env;
          full_env.set("XDG_RUNTIME_DIR", runtime_dir);

          auto pulse_sink_name = fmt::format("virtual_sink_{}", session->session_id);
          auto audio_server_name = audio_server ? audio::get_server_name(audio_server->server) : "";
          full_env.set("PULSE_SINK", pulse_sink_name);
          full_env.set("PULSE_SOURCE", pulse_sink_name + ".monitor");
          full_env.set("PULSE_SERVER", audio_server_name);
          mounted_paths.push_back({audio_server_name, audio_server_name});

          if (session->app->start_virtual_compositor) {
            auto wl_state = *session->wayland_display->load();
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
          }

          /* Adding custom state folder */
          mounted_paths.push_back({session->app_state_folder, "/home/retro"});

          /* GPU specific adjustments */
          auto render_node = session->app->render_node;
          auto additional_devices = linked_devices(render_node);
          std::copy(additional_devices.begin(), additional_devices.end(), std::back_inserter(all_devices));

          auto gpu_vendor = get_vendor(render_node);
          if (gpu_vendor == NVIDIA) {
            if (auto driver_volume = utils::get_env("NVIDIA_DRIVER_VOLUME_NAME")) {
              logs::log(logs::info, "Mounting nvidia driver {}:/usr/nvidia", driver_volume);
              mounted_paths.push_back({driver_volume, "/usr/nvidia"});
            }
          } else if (gpu_vendor == INTEL) {
            full_env.set("INTEL_DEBUG", "norbc"); // see: https://github.com/games-on-whales/wolf/issues/50
          }

          full_env.set("PUID", std::to_string(session->client_settings->run_uid));
          full_env.set("PGID", std::to_string(session->client_settings->run_gid));

          auto devices_q = plugged_devices_queue->load()->find(session->session_id);
          if (!devices_q) {
            logs::log(logs::warning, "No devices queue found for session {}", session->session_id);
            return;
          } else {
            /* Finally run the app, this will stop here until over */
            run_session->runner->run(session->session_id,
                                     session->app_state_folder,
                                     *devices_q,
                                     all_devices.persistent(),
                                     mounted_paths.persistent(),
                                     full_env.persistent(),
                                     render_node);
          }

          if (run_session->stop_stream_when_over) {
            /* App exited, cleanup */
            logs::log(logs::debug, "[STREAM_SESSION] Remove virtual audio sink");
            if (session->app->start_audio_server) {
              audio::delete_virtual_sink(audio_server->server, session->audio_sink->load());
            }

            session->wayland_display->store(nullptr);

            app_state->event_bus->fire_event(
                immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = session->session_id}));
          }
        }).detach();
      }));

  // Video streaming pipeline
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::VideoSession>>(
      [=](const immer::box<events::VideoSession> &sess) {
        std::thread([=]() {
          boost::promise<unsigned short> port_promise;
          auto port_fut = port_promise.get_future();
          std::once_flag called;
          auto ev_handler = app_state->event_bus->register_handler<immer::box<events::RTPVideoPingEvent>>(
              [pp = std::ref(port_promise), &called, sess](const immer::box<events::RTPVideoPingEvent> &ping_ev) {
                std::call_once(called, [=]() { // We'll keep receiving PING requests, but we only want the first one
                  if (ping_ev->client_ip == sess->client_ip) {
                    pp.get().set_value(ping_ev->client_port); // This throws when set multiple times
                  }
                });
              });

          std::shared_ptr<std::atomic_bool> cancel_job = std::make_shared<std::atomic<bool>>(false);
          auto cancel_event = app_state->event_bus->register_handler<immer::box<events::VideoSession>>(
              [=](const immer::box<events::VideoSession> &new_sess) {
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

          streaming::start_streaming_video(sess, app_state->event_bus, client_port);
        }).detach();
      }));

  // Audio streaming pipeline
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::AudioSession>>(
      [=](const immer::box<events::AudioSession> &sess) {
        std::thread([=]() {
          boost::promise<unsigned short> port_promise;
          auto port_fut = port_promise.get_future();
          std::once_flag called;
          auto ev_handler = app_state->event_bus->register_handler<immer::box<events::RTPAudioPingEvent>>(
              [pp = std::ref(port_promise), &called, sess](const immer::box<events::RTPAudioPingEvent> &ping_ev) {
                std::call_once(called, [=]() { // We'll keep receiving PING requests, but we only want the first one
                  if (ping_ev->client_ip == sess->client_ip) {
                    pp.get().set_value(ping_ev->client_port); // This throws when set multiple times
                  }
                });
              });

          std::shared_ptr<std::atomic_bool> cancel_job = std::make_shared<std::atomic<bool>>(false);
          auto cancel_event = app_state->event_bus->register_handler<immer::box<events::AudioSession>>(
              [=](const immer::box<events::AudioSession> &new_sess) {
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
  std::thread([sessions = local_state->running_sessions]() {
    rtsp::run_server(state::RTSP_SETUP_PORT, sessions);
  }).detach();

  // Control
  std::thread([sessions = local_state->running_sessions, ev_bus = local_state->event_bus]() {
    control::run_control(state::CONTROL_PORT, sessions, ev_bus);
  }).detach();

  // Wolf API server
  std::thread([local_state]() { wolf::api::start_server(local_state); }).detach();

  // mDNS
  try {
    mdns_cpp::Logger::setLoggerSink([](const std::string &msg) {
      // msg here will include a /n at the end, so we remove it
      logs::log(logs::trace, "mDNS: {}", msg.substr(0, msg.size() - 1));
    });
    mdns_cpp::mDNS mdns;
    mdns.setServiceName("_nvstream._tcp.local.");
    mdns.setServiceHostname(local_state->config->hostname);
    mdns.setServicePort(state::HTTP_PORT);
    mdns.startService();
  } catch (const std::exception &e) {
    logs::log(logs::error, "mDNS error: {}", e.what());
  }

  auto audio_server = setup_audio_server(runtime_dir);
  auto sess_handlers = setup_sessions_handlers(local_state, runtime_dir, audio_server);

  http_thread.join(); // Let's park the main thread over here
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