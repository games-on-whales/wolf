#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <control/control.hpp>
#include <csignal>
#include <fstream>
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
auto initialize(std::string_view config_file, std::string_view pkey_filename, std::string_view cert_filename) {
  auto config = load_config(config_file);
  auto display_modes = getDisplayModes();

  auto host = get_host_config(pkey_filename, cert_filename);
  auto atom = new immer::atom<immer::map<std::string, state::PairCache>>();
  state::AppState state = {config, host, *atom, std::make_shared<dp::event_bus>()};
  return std::make_shared<state::AppState>(state);
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

const char *get_env(const char *tag, const char *def = nullptr) noexcept {
  const char *ret = std::getenv(tag);
  return ret ? ret : def;
}

/**
 * A bit tricky here: on a basic level we want a map of [session_id] -> thread_pool
 */
typedef immer::atom<immer::map<std::size_t, std::shared_ptr<boost::asio::thread_pool>>> TreadsMapAtom;

/**
 * Glue code in order to start the sessions in response to events fired in the bus
 *
 * @param event_bus: the shared bus where the handlers will be registered
 * @param threads: holds reference to all running threads, grouped by session_id
 *
 * @returns a list of signals handlers that have been created
 */
auto setup_sessions_handlers(std::shared_ptr<dp::event_bus> &event_bus, TreadsMapAtom &threads) {
  immer::vector_transient<immer::box<dp::handler_registration>> handlers;

  // RTSP
  handlers.push_back(event_bus->register_handler<immer::box<state::StreamSession>>(
      [&threads](immer::box<state::StreamSession> stream_session) {
        // Create pool
        auto t_pool = std::make_shared<ba::thread_pool>(6);

        // Start RTSP
        ba::post(*t_pool, [stream_session]() { rtsp::run_server(stream_session->rtsp_port, stream_session); });

        // Store pool for others
        threads.update([sess_id = stream_session->session_id, t_pool](auto t_map) {
          return t_map.update(sess_id, [t_pool](auto box) { return t_pool; });
        });
      }));

  // Control thread
  control::init(); // Need to initialise enet once
  handlers.push_back(event_bus->register_handler<immer::box<state::ControlSession>>(
      [&threads](immer::box<state::ControlSession> control_sess) {
        auto t_pool = threads.load()->at(control_sess->session_id);

        // Start control stream
        ba::post(*t_pool, [control_sess]() {
          int timeout_millis = 1000; // TODO: config?
          control::run_control(control_sess, timeout_millis);
        });
      }));

  handlers.push_back(event_bus->register_handler<immer::box<state::LaunchAPPEvent>>(
      [&threads](immer::box<state::LaunchAPPEvent> launch_ev) {
        auto t_pool = threads.load()->at(launch_ev->session_id);

        // Start selected app
        ba::post(*t_pool, [launch_ev]() { process::run_process(launch_ev); });
      }));

  // GStreamer video
  streaming::init(); // Need to initialise streaming once
  handlers.push_back(event_bus->register_handler<immer::box<state::VideoSession>>(
      [&threads](immer::box<state::VideoSession> video_sess) {
        auto t_pool = threads.load()->at(video_sess->session_id);

        ba::post(*t_pool, [video_sess, t_pool]() {
          auto client_port = rtp::wait_for_ping(video_sess->port);
          streaming::start_streaming_video(std::move(video_sess), client_port, t_pool);
        });
      }));

  // GStreamer audio
  handlers.push_back(event_bus->register_handler<immer::box<state::AudioSession>>(
      [&threads](immer::box<state::AudioSession> audio_sess) {
        auto t_pool = threads.load()->at(audio_sess->session_id);

        ba::post(*t_pool, [audio_sess]() {
          auto client_port = rtp::wait_for_ping(audio_sess->port);
          streaming::start_streaming_audio(std::move(audio_sess), client_port);
        });
      }));

  // On control session end, let's wait for all threads to finish then clean them up
  handlers.push_back(event_bus->register_handler<immer::box<moonlight::control::TerminateEvent>>(
      [&threads](immer::box<moonlight::control::TerminateEvent> event) {
        // Events are dispatched from the calling thread; in this case it'll be the control stream thread.
        // We have to create a new thread to process the cleaning and detach it from the original thread
        auto cleanup_thread = std::thread([&threads, sess_id = event->session_id]() {
          auto t_pool = threads.load()->at(sess_id);
          logs::log(logs::debug, "Terminated session: {}, waiting for thread_pool to finish", sess_id);

          t_pool->wait();

          logs::log(logs::info, "Closed session: {}", sess_id);
          threads.update([sess_id](auto t_map) { return t_map.erase(sess_id); });
        });

        cleanup_thread.detach();
      }));

  return handlers.persistent();
}

/**
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::parse_level(get_env("LOG_LEVEL", "INFO")));
  check_exceptions();

  auto config_file = get_env("CFG_FILE", "config.toml");
  auto p_key_file = get_env("PRIVATE_KEY_FILE", "key.pem");
  auto p_cert_file = get_env("PRIVATE_CERT_FILE", "cert.pem");
  auto local_state = initialize(config_file, p_key_file, p_cert_file);

  // REST HTTP/S APIs
  auto http_server = std::make_unique<HttpServer>();
  auto http_thread = HTTPServers::startServer(http_server.get(), local_state, state::HTTP_PORT);

  auto https_server = std::make_unique<HttpsServer>(p_cert_file, p_key_file);
  auto https_thread = HTTPServers::startServer(https_server.get(), local_state, state::HTTPS_PORT);

  // Holds reference to all running threads, grouped by session_id
  auto threads = TreadsMapAtom{};
  // RTSP, RTP, Control and all the rest of Moonlight protocol will be started/stopped on demand via HTTPS
  auto sess_handlers = setup_sessions_handlers(local_state->event_bus, threads);

  // Exception and termination handling
  shutdown_handler = [&sess_handlers, &threads](int signum) {
    logs::log(logs::info, "Received interrupt signal {}, clean exit", signum);
    if (signum == SIGABRT || signum == SIGSEGV) {
      auto trace_file = "./backtrace.dump";
      logs::log(logs::error, "Runtime error, dumping stacktrace to {}", trace_file);
      boost::stacktrace::safe_dump_to(trace_file);
    }

    for (const auto &thread_pool : *threads.load()) {
      thread_pool.second->stop();
    }

    for (const auto &handler : sess_handlers) {
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

  // Let's park the main thread over here, HTTP/S should never stop
  https_thread.join();
  http_thread.join();
}