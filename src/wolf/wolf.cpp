#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <control/control.hpp>
#include <csignal>
#include <fstream>
#include <immer/array.hpp>
#include <input/input.hpp>
#include <memory>
#include <rest/rest.hpp>
#include <rtp/udp-ping.hpp>
#include <rtsp/net.hpp>
#include <state/config.hpp>
#include <streaming/streaming.hpp>
#include <vector>

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
    logs::log(logs::debug, "Loading server certificates from disk...");
    server_cert = x509::cert_from_file(cert_filename);
    server_pkey = x509::pkey_from_file(pkey_filename);
  } else {
    logs::log(logs::info, "x509 certificates not present, generating...");
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

void user_pin_handler(state::PairSignal pair_request) {
  std::string user_pin;
  std::cout << "Insert pin:" << std::endl;
  std::getline(std::cin, user_pin);
  pair_request.user_pin.set_value(user_pin);
}

/**
 * A bit tricky here: on a basic level we want a map of [session_id] -> std::thread
 *
 * Unfortunately std::thread doesn't have a copy constructor AND `.join()` is not marked as `const`
 * So we wrap the thread with a immer box in order to be able to store it in the vector (copy constructor)
 * AND we wrap it in a unique_ptr so that we can access it without `const` (.join())
 */
typedef immer::atom<immer::map<std::size_t, immer::vector<immer::box<std::unique_ptr<std::thread>>>>> TreadsMapAtom;

/**
 * Glue code in order to start the sessions in response to events fired in the bus
 *
 * @param event_bus: the shared bus where the handlers will be registered
 * @param threads: holds reference to all running threads, grouped by session_id
 *
 * @returns a list of signals handlers that have been created
 */
auto setup_sessions_handlers(std::shared_ptr<dp::event_bus> &event_bus, TreadsMapAtom &threads) {
  // HTTP PIN
  auto pair_sig = event_bus->register_handler<state::PairSignal>(&user_pin_handler);

  // RTSP
  auto rtsp_launch_sig = event_bus->register_handler<immer::box<state::StreamSession>>(
      [&threads](immer::box<state::StreamSession> stream_session) {
        auto thread = rtsp::start_server(stream_session->rtsp_port, stream_session);
        auto thread_ptr = std::make_unique<std::thread>(std::move(thread));

        threads.update([&thread_ptr, sess_id = stream_session->session_id](auto t_map) {
          return t_map.update(sess_id, [&thread_ptr](auto t_vec) { return t_vec.push_back(std::move(thread_ptr)); });
        });
      });

  // Control thread
  control::init(); // Need to initialise enet once
  auto ctrl_launch_sig = event_bus->register_handler<immer::box<state::ControlSession>>(
      [&threads](immer::box<state::ControlSession> control_sess) {
        auto thread = control::start_service(control_sess);
        auto thread_ptr = std::make_unique<std::thread>(std::move(thread));

        threads.update([&thread_ptr, sess_id = control_sess->session_id](auto t_map) {
          return t_map.update(sess_id, [&thread_ptr](auto t_vec) { return t_vec.push_back(std::move(thread_ptr)); });
        });
      });

  // GStreamer video
  streaming::init(); // Need to initialise streaming once
  auto video_launch_sig = event_bus->register_handler<immer::box<state::VideoSession>>(
      [&threads](immer::box<state::VideoSession> video_sess) {
        auto thread = std::thread(
            [](auto video_sess) {
              auto client_port = rtp::wait_for_ping(video_sess->port);
              streaming::start_streaming_video(std::move(video_sess), client_port);
            },
            video_sess);
        auto thread_ptr = std::make_unique<std::thread>(std::move(thread));

        threads.update([&thread_ptr, sess_id = video_sess->session_id](auto t_map) {
          return t_map.update(sess_id, [&thread_ptr](auto t_vec) { return t_vec.push_back(std::move(thread_ptr)); });
        });
      });

  // GStreamer audio
  auto audio_launch_sig = event_bus->register_handler<immer::box<state::AudioSession>>(
      [&threads](immer::box<state::AudioSession> audio_sess) {
        auto thread = std::thread(
            [](auto audio_sess) {
              auto client_port = rtp::wait_for_ping(audio_sess->port);
              streaming::start_streaming_audio(std::move(audio_sess), client_port);
            },
            audio_sess);
        auto thread_ptr = std::make_unique<std::thread>(std::move(thread));

        threads.update([&thread_ptr, sess_id = audio_sess->session_id](auto t_map) {
          return t_map.update(sess_id, [&thread_ptr](auto t_vec) { return t_vec.push_back(std::move(thread_ptr)); });
        });
      });

  // On control session end, let's wait for all threads to finish then clean them up
  auto ctrl_handler = event_bus->register_handler<immer::box<moonlight::control::TerminateEvent>>(
      [&threads](immer::box<moonlight::control::TerminateEvent> event) {
        // Events are dispatched from the calling thread; in this case it'll be the control stream thread.
        // We have to create a new thread to process the cleaning and detach it from the original thread
        auto cleanup_thread = std::thread([&threads, sess_id = event->session_id]() {
          auto t_vec = threads.load()->at(sess_id);

          logs::log(logs::debug, "Terminated session: {}, waiting for {} threads to finish", sess_id, t_vec.size());

          for (const auto &t_ptr : t_vec) {
            auto thread = t_ptr->get();
            thread->join(); // Wait for the thread to be over
          }

          logs::log(logs::debug, "Removing session: {}", sess_id);
          threads.update([sess_id](auto t_map) { return t_map.erase(sess_id); });
        });
        cleanup_thread.detach();
      });

  return immer::array<immer::box<dp::handler_registration>>{std::move(pair_sig),
                                                            std::move(rtsp_launch_sig),
                                                            std::move(ctrl_launch_sig),
                                                            std::move(video_launch_sig),
                                                            std::move(audio_launch_sig),
                                                            std::move(ctrl_handler)};
}

/**
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::parse_level(get_env("LOG_LEVEL", "INFO")));
  check_exceptions();

  auto config_file = get_env("CFG_FILE", "config.toml");
  auto local_state = initialize(config_file, "key.pem", "cert.pem");

  // REST HTTP/S APIs
  auto http_server = std::make_unique<HttpServer>();
  auto http_thread = HTTPServers::startServer(http_server.get(), local_state, state::HTTP_PORT);

  auto https_server = std::make_unique<HttpsServer>("cert.pem", "key.pem");
  auto https_thread = HTTPServers::startServer(https_server.get(), local_state, state::HTTPS_PORT);

  // Holds reference to all running threads, grouped by session_id
  auto threads = TreadsMapAtom{};
  // RTSP, RTP, Control and all the rest of Moonlight protocol will be started/stopped on demand via HTTPS
  auto sess_handlers = setup_sessions_handlers(local_state->event_bus, threads);

  // Exception and termination handling
  shutdown_handler = [&sess_handlers](int signum) {
    logs::log(logs::info, "Received interrupt signal {}, clean exit", signum);
    if (signum == SIGABRT || signum == SIGSEGV) {
      auto trace_file = "./backtrace.dump";
      logs::log(logs::error, "Runtime error, dumping stacktrace to {}", trace_file);
      boost::stacktrace::safe_dump_to(trace_file);
    }

    for (const auto &handler : sess_handlers) {
      handler->unregister();
    }

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