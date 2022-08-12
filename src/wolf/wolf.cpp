#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <control/control.cpp>
#include <csignal>
#include <helpers/logger.hpp>
#include <immer/array.hpp>
#include <memory>
#include <moonlight/data-structures.hpp>
#include <rest/servers.cpp>
#include <rtsp/rtsp.cpp>
#include <state/config.hpp>
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
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::parse_level(get_env("LOG_LEVEL", "INFO")));
  check_exceptions();

  auto config_file = "config.json";
  auto local_state = initialize(config_file, "key.pem", "cert.pem");

  auto pair_sig = local_state->event_bus->register_handler<state::PairSignal>(&user_pin_handler);

  auto https_server = std::make_unique<HttpsServer>("cert.pem", "key.pem", local_state);
  auto http_server = std::make_unique<HttpServer>();

  auto https_port = local_state->config.base_port + state::HTTPS_PORT;
  auto https_thread = HTTPServers::startServer(https_server.get(), local_state, https_port);

  auto http_port = local_state->config.base_port + state::HTTP_PORT;
  auto http_thread = HTTPServers::startServer(http_server.get(), local_state, http_port);

  std::vector<std::thread> thread_pool;
  auto rtsp_launch_sig = local_state->event_bus->register_handler<immer::box<state::StreamSession>>(
      [&thread_pool](immer::box<state::StreamSession> stream_session) {
        auto port = stream_session->rtsp_port;
        thread_pool.push_back(rtsp::start_server(port, std::move(stream_session)));
      });

  control::init(); // Need to initialise enet once
  auto ctrl_launch_sig = local_state->event_bus->register_handler<immer::box<state::ControlSession>>(
      [&thread_pool](immer::box<state::ControlSession> control_sess) {
        thread_pool.push_back(control::start_service(std::move(control_sess)));
      });

  // GStreamer test
  //  streaming::init(argc, argv);
  //  logs::log(logs::debug, "Initialised gstreamer: {}", streaming::version());
  //  streaming::play("videotestsrc", "autovideosink");

  // Exception and termination handling
  shutdown_handler = [&local_state, &config_file](int signum) {
    logs::log(logs::info, "Received interrupt signal {}, clean exit", signum);
    if (signum == SIGABRT || signum == SIGSEGV) {
      auto trace_file = "./backtrace.dump";
      logs::log(logs::error, "Runtime error, dumping stacktrace to {}", trace_file);
      boost::stacktrace::safe_dump_to(trace_file);
    }

    logs::log(logs::debug, "Saving back current configuration to file: {}", config_file);
    state::save(local_state->config, config_file);

    exit(signum);
  };
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGQUIT, signal_handler);
  std::signal(SIGSEGV, signal_handler);
  std::signal(SIGABRT, signal_handler);

  https_thread.join();
  http_thread.join();
}