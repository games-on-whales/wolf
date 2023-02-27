#include <audio/audio.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <chrono>
#include <control/control.hpp>
#include <csignal>
#include <docker/docker.hpp>
#include <immer/array.hpp>
#include <immer/vector_transient.hpp>
#include <memory>
#include <process/process.hpp>
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
auto load_config(std::string_view config_file) {
  logs::log(logs::info, "Reading config file from: {}", config_file);
  return state::load_or_default(config_file.data());
}

/**
 * @brief Get the Display Modes
 * TODO: get them from the host properly
 */
immer::array<moonlight::DisplayMode> getDisplayModes() {
  return {{1920, 1080, 60}, {1024, 768, 30}};
}

/**
 * @brief Get the Audio Modes
 * TODO: get them from the host properly
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
auto initialize(std::string_view config_file,
                int t_pool_size,
                std::string_view pkey_filename,
                std::string_view cert_filename) {
  auto config = load_config(config_file);
  auto display_modes = getDisplayModes();

  auto host = get_host_config(pkey_filename, cert_filename);
  auto state = state::AppState{
      .config = config,
      .host = host,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .event_bus = std::make_shared<dp::event_bus>(),
      .running_sessions = std::make_shared<immer::atom<immer::vector<state::StreamSession>>>(),
      .t_pool = std::make_shared<boost::asio::thread_pool>(t_pool_size)};
  return immer::box<state::AppState>(state);
}

/**
 * Taken from: https://stackoverflow.com/questions/11468414/using-auto-and-lambda-to-handle-signal
 * in order to have pass a lambda with variable capturing
 */
namespace {
std::function<void(int)> shutdown_handler;
void signal_handler(int signal) {
  shutdown_handler(signal);
}
} // namespace

/**
 * @brief: if an exception was raised we should have created a dump file, here we can pretty print it
 */
void check_exceptions() {
  if (boost::filesystem::exists("./backtrace.dump")) {
    std::ifstream ifs("./backtrace.dump");

    auto st = boost::stacktrace::stacktrace::from_dump(ifs);
    logs::log(logs::error, "Previous run crashed: \n{}", to_string(st));

    // cleaning up
    ifs.close();
    boost::filesystem::remove("./backtrace.dump");
  }
}

auto setup_sessions_handlers(const immer::box<state::AppState> &app_state) {
  immer::vector_transient<immer::box<dp::handler_registration>> handlers;
  auto t_pool = app_state->t_pool;

  // Run process when the event is raised
  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::LaunchAPPEvent>>(
      [t_pool](const immer::box<state::LaunchAPPEvent> &launch_ev) {
        // Start selected app
        ba::post(*t_pool, [launch_ev]() { process::run_process(launch_ev); });
      }));

  /* Audio/Video streaming */
  auto video_sessions = std::make_shared<immer::atom<immer::map<std::string, immer::box<state::VideoSession>>>>();
  auto audio_sessions = std::make_shared<immer::atom<immer::map<std::string, immer::box<state::AudioSession>>>>();

  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::VideoSession>>(
      [video_sessions](const immer::box<state::VideoSession> &sess) {
        video_sessions->update([=](const auto map) { return map.set(sess->client_ip, sess); });
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<state::AudioSession>>(
      [audio_sessions](const immer::box<state::AudioSession> &sess) {
        audio_sessions->update([=](const auto map) { return map.set(sess->client_ip, sess); });
      }));

  ba::post(*t_pool, [=]() {
    rtp::wait_for_ping(state::VIDEO_PING_PORT, [=](unsigned short client_port, const std::string &client_ip) {
      auto video_sess = video_sessions->load()->find(client_ip);
      if (video_sess != nullptr) {
        ba::post(*t_pool, [=, sess = *video_sess]() {
          streaming::start_streaming_video(sess, app_state->event_bus, client_port, t_pool);
        });
        video_sessions->update([&client_ip](const auto &map) { return map.erase(client_ip); });
      }
    });
  });

  ba::post(*t_pool, [=]() {
    auto audio_server = audio::connect(*t_pool);

    rtp::wait_for_ping(state::AUDIO_PING_PORT, [=](unsigned short client_port, const std::string &client_ip) {
      auto audio_sess = audio_sessions->load()->find(client_ip);
      if (audio_sess != nullptr) {
        ba::post(*t_pool, [=, sess = *audio_sess]() {
          /* Create virtual audio device */
          auto sink_name = fmt::format("virtual_sink_{}", sess->session_id);
          auto v_device = audio::create_virtual_sink(
              audio_server,
              audio::AudioDevice{.sink_name = sink_name, .n_channels = sess->channels, .bitrate = sess->bitrate});

          /* Start the gstreamer pipeline */
          streaming::start_streaming_audio(sess, app_state->event_bus, client_port, sink_name + ".monitor");

          /* Clean, remove virtual audio device */
          audio::delete_virtual_sink(audio_server, v_device);
        });
        audio_sessions->update([&client_ip](const auto &map) { return map.erase(client_ip); });
      }
    });
  });

  return std::make_pair(handlers.persistent(), t_pool);
}

/**
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::parse_level(get_env("WOLF_LOG_LEVEL", "INFO")));
  check_exceptions();

  streaming::init(); // Need to initialise gstreamer once
  control::init();   // Need to initialise enet once
  docker::init();    // Need to initialise libcurl once

  auto config_file = get_env("WOLF_CFG_FILE", "config.toml");
  auto p_key_file = get_env("WOLF_PRIVATE_KEY_FILE", "key.pem");
  auto p_cert_file = get_env("WOLF_PRIVATE_CERT_FILE", "cert.pem");
  auto local_state =
      initialize(config_file, std::stoi(get_env("WOLF_THREAD_POOL_SIZE", "30")), p_key_file, p_cert_file);

  // HTTP APIs
  ba::post(*local_state->t_pool, [local_state]() {
    HttpServer server = HttpServer();
    HTTPServers::startServer(&server, local_state, state::HTTP_PORT);
  });

  // HTTPS APIs
  ba::post(*local_state->t_pool, [local_state, p_key_file, p_cert_file]() {
    HttpsServer server = HttpsServer(p_cert_file, p_key_file);
    HTTPServers::startServer(&server, local_state, state::HTTPS_PORT);
  });

  // RTSP
  ba::post(*local_state->t_pool, [sessions = local_state->running_sessions, ev_bus = local_state->event_bus]() {
    rtsp::run_server(state::RTSP_SETUP_PORT, sessions, ev_bus);
  });

  // Control
  ba::post(*local_state->t_pool, [sessions = local_state->running_sessions, ev_bus = local_state->event_bus]() {
    control::run_control(state::CONTROL_PORT, sessions, ev_bus);
  });

  auto sess_handlers = setup_sessions_handlers(local_state);

  // Exception and termination handling
  shutdown_handler = [&sess_handlers](int signum) {
    logs::log(logs::info, "Received interrupt signal {}, clean exit", signum);
    if (signum == SIGABRT || signum == SIGSEGV) {
      auto trace_file = "./backtrace.dump";
      logs::log(logs::error, "Runtime error, dumping stacktrace to {}", trace_file);
      boost::stacktrace::safe_dump_to(trace_file);
    }

    // Stop the thread pool
    sess_handlers.second->stop();

    for (const auto &handler : sess_handlers.first) {
      handler->unregister();
    }

    logs::log(logs::info, "See ya!");
    exit(signum);
  };
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGQUIT, signal_handler);
  std::signal(SIGSEGV, signal_handler);
  std::signal(SIGABRT, signal_handler);

  // Let's park the main thread over here
  local_state->t_pool->wait();
}