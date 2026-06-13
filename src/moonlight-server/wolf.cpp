#include <api/api.hpp>
#include <atomic>
#include <audio/pulse_router.hpp>
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
#include <introspect/introspect.hpp>
#include <mdns_cpp/logger.hpp>
#include <mdns_cpp/mdns.hpp>
#include <memory>
#include <rest/rest.hpp>
#include <rtsp/net.hpp>
#include <sessions/handlers.hpp>
#include <state/config.hpp>
#include <streaming/streaming.hpp>
#include <vector>

namespace ba = boost::asio;
namespace fs = std::filesystem;

using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace wolf::core;

/**
 * Set by the SIGINT/SIGTERM handler. We can't touch AppState (or do anything
 * that isn't async-signal-safe) from inside the handler, so it just flips this
 * flag and the main thread performs the actual graceful shutdown.
 */
static std::atomic_bool shutdown_requested = false;

static void graceful_shutdown_handler(int signum) {
  shutdown_requested.store(true, std::memory_order_relaxed);
}

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

  std::string local_base_state_folder = utils::get_env("HOST_APPS_STATE_FOLDER", "/etc/wolf");
  std::string host_base_state_folder = local_base_state_folder;
  std::string host_xdg_runtime_dir = utils::get_env("XDG_RUNTIME_DIR", "/tmp/sockets");

  docker::DockerAPI docker_api(utils::get_env("WOLF_DOCKER_SOCKET", "/var/run/docker.sock"));
  if (auto container = introspect::get_current_container(docker_api)) {
    host_base_state_folder =
        introspect::get_host_path_for(*container, local_base_state_folder).value_or(local_base_state_folder);
    host_xdg_runtime_dir =
        introspect::get_host_path_for(*container, host_xdg_runtime_dir).value_or(host_xdg_runtime_dir);
  } else {
    logs::log(logs::warning,
              "Unable to get the container that is running Wolf, automatic mounts matching is disabled.");
  }

  return {state::DISPLAY_CONFIGURATIONS,
          state::AUDIO_CONFIGURATIONS,
          server_cert,
          server_pkey,
          internal_ip,
          mac_address,
          host_base_state_folder,
          local_base_state_folder,
          host_xdg_runtime_dir};
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
      .lobbies = std::make_shared<immer::atom<immer::vector<events::Lobby>>>(),
      .running_sessions = running_sessions};
  return immer::box<state::AppState>(state);
}

/**
 * We first try to connect to a running PulseAudio server
 * if that fails, we run our own PulseAudio container and connect to it
 * if that fails, we can't return an AudioServer, hence the optional!
 */
std::optional<sessions::AudioServer> setup_audio_server(const std::string &host_runtime_dir,
                                                        const std::string &runtime_dir) {
  auto audio_server = audio::connect();
  if (audio::connected(audio_server)) {
    return {{.server = audio_server}};
  } else {
    logs::log(logs::info, "Starting PulseAudio docker container");
    docker::DockerAPI docker_api(utils::get_env("WOLF_DOCKER_SOCKET", "/var/run/docker.sock"));
    auto pulse_socket = fmt::format("{}/pulse-socket", runtime_dir);

    /* Cleanup old leftovers, Pulse will fail to start otherwise */
    try {
      std::filesystem::remove(pulse_socket);
      std::filesystem::remove_all(fmt::format("{}/pulse", runtime_dir));
    } catch (const std::filesystem::filesystem_error &e) {
      logs::log(logs::warning, "Failed to remove old PulseAudio socket: {}", e.what());
    }

    auto container = docker_api.create(
        docker::Container{
            .id = "",
            .name = "WolfPulseAudio",
            .image = utils::get_env("WOLF_PULSE_IMAGE", "ghcr.io/games-on-whales/pulseaudio:master"),
            .status = docker::CREATED,
            .ports = {},
            .mounts = {docker::MountPoint{.source = host_runtime_dir, .destination = "/tmp/pulse/", .mode = "rw"}},
            .env = {"XDG_RUNTIME_DIR=/tmp/pulse/", "UNAME=retro", "UID=1000", "GID=1000", "HOME=/home/retro"}},
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

/**
 * @brief here's where the magic starts
 */
void run() {
  streaming::init(); // Need to initialise gstreamer once
  control::init();   // Need to initialise enet once
  docker::init();    // Need to initialise libcurl once
  gst_video_context::init();

  auto runtime_dir = utils::get_env("XDG_RUNTIME_DIR", "/tmp/sockets");
  logs::log(logs::debug, "XDG_RUNTIME_DIR={}", runtime_dir);

  auto config_file = utils::get_env("WOLF_CFG_FILE", "config.toml");
  auto p_key_file = utils::get_env("WOLF_PRIVATE_KEY_FILE", "key.pem");
  auto p_cert_file = utils::get_env("WOLF_PRIVATE_CERT_FILE", "cert.pem");
  auto local_state = initialize(config_file, p_key_file, p_cert_file);

  // HTTP APIs
  std::thread([local_state]() {
    HttpServer server = HttpServer();
    HTTPServers::startServer(&server, local_state, state::get_port(state::HTTP_PORT));
  }).detach();

  // HTTPS APIs
  std::thread([local_state, p_key_file, p_cert_file]() {
    HttpsServer server = HttpsServer(p_cert_file, p_key_file);
    HTTPServers::startServer(&server, local_state, state::get_port(state::HTTPS_PORT));
  }).detach();

  // RTSP
  std::thread([sessions = local_state->running_sessions]() {
    rtsp::run_server(state::get_port(state::RTSP_SETUP_PORT), sessions);
  }).detach();

  // Control
  std::thread([sessions = local_state->running_sessions, ev_bus = local_state->event_bus]() {
    control::run_control(state::get_port(state::CONTROL_PORT), sessions, ev_bus);
  }).detach();

  // RTP
  rtp::start_rtp_ping(state::get_port(state::VIDEO_PING_PORT),
                      state::get_port(state::AUDIO_PING_PORT),
                      local_state->event_bus);
  // Wolf API server
  std::thread([local_state, runtime_dir]() { wolf::api::start_server(runtime_dir, local_state); }).detach();

  // mDNS
  std::thread([hostname = local_state->config->hostname]() {
    logs::log(logs::info, "Starting mDNS service");
    try {
      mdns_cpp::Logger::setLoggerSink([](const std::string &msg) {
        // msg here will include a /n at the end, so we remove it
        logs::log(logs::trace, "mDNS: {}", msg.substr(0, msg.size() - 1));
      });
      mdns_cpp::mDNS mdns;
      mdns.setServiceName("_nvstream._tcp.local.");
      mdns.setServiceHostname(hostname);
      mdns.setServicePort(state::HTTP_PORT);
      mdns.startService(false);
    } catch (const std::exception &e) {
      logs::log(logs::error, "mDNS error: {}", e.what());
    }
  }).detach();

  auto audio_server = setup_audio_server(local_state->host->host_xdg_runtime_dir, runtime_dir);
  // PulseAudio sink-input router (hostname -> session_id -> virtual_sink_<session>)
  auto pulse_router_state = std::make_shared<audio::PulseAudioRouterState>(audio_server->server);
  auto pulse_router_handlers = audio::setup_pulseaudio_router_handlers(local_state, pulse_router_state);
  // Setup event handlers for Moonlight related events (Start/Stop stream, hotplug, etc)
  auto moonlight_sess_handlers = sessions::setup_moonlight_handlers(local_state, runtime_dir, audio_server);
  // Setup event handlers for player Lobbies
  auto lobbies_handlers = sessions::setup_lobbies_handlers(local_state, runtime_dir, audio_server);

  // Park the main thread until a termination signal arrives. supervisord sends
  // SIGINT on container stop (stopsignal=INT) and waits stopwaitsecs for us to
  // exit, so we use that window to tear everything down cleanly.
  while (!shutdown_requested.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(200ms);
  }

  logs::log(logs::info, "Received shutdown signal, stopping all sessions and lobbies");

  // Stop lobbies first: each one makes its connected sessions leave gracefully.
  for (const auto &lobby : local_state->lobbies->load().get()) {
    local_state->event_bus->fire_event(
        immer::box<events::StopLobbyEvent>(events::StopLobbyEvent{.lobby_id = lobby.id}));
  }

  // Then stop any remaining standalone streams.
  for (const auto &session : local_state->running_sessions->load().get()) {
    local_state->event_bus->fire_event(
        immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = session.session_id}));
  }

  // fire_event is synchronous, but container/pipeline teardown can still settle
  // asynchronously; wait (bounded) for the state to drain before we exit.
  for (int i = 0;
       i < 100 && (!local_state->running_sessions->load().get().empty() || !local_state->lobbies->load().get().empty());
       i++) {
    std::this_thread::sleep_for(100ms);
  }

  logs::log(logs::info, "Graceful shutdown complete");
}

int main(int argc, char *argv[]) try {
  logs::init(logs::parse_level(utils::get_env("WOLF_LOG_LEVEL", "INFO")));
  // Graceful termination: stop all sessions/lobbies before exiting (see run()).
  std::signal(SIGINT, graceful_shutdown_handler);
  std::signal(SIGTERM, graceful_shutdown_handler);
  // Crash/abort handling: dump a stacktrace and exit immediately.
  std::signal(SIGQUIT, shutdown_handler);
  std::signal(SIGSEGV, shutdown_handler);
  std::signal(SIGABRT, shutdown_handler);
  std::set_terminate(on_terminate);
  check_exceptions();

  run(); // Main loop
} catch (...) {
  on_terminate();
}
