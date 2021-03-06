#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <csignal>
#include <helpers/logger.hpp>
#include <moonlight/config.hpp>
#include <moonlight/data-structures.hpp>
#include <rest/servers.cpp>
#include <vector>

/**
 * @brief Will try to load the config file and fallback to defaults
 */
std::shared_ptr<moonlight::Config> load_config(const std::string &config_file) {
  logs::log(logs::info, "Reading config file from: {}", config_file);
  try {
    return std::make_shared<moonlight::Config>(config_file);
  } catch (const std::exception &e) {
    logs::log(logs::warning, "Unable to open config file: {}, using defaults", config_file);
    pt::ptree defaults;

    defaults.put("base_port", 47989);
    // TODO: external_ip, local_ip, mac_address

    return std::make_shared<moonlight::Config>(defaults);
  }
}

/**
 * @brief Get the Display Modes
 * TODO: get them using pipewire
 */
std::shared_ptr<std::vector<moonlight::DisplayMode>> getDisplayModes() {
  std::vector<moonlight::DisplayMode> displayModes = {{1920, 1080, 60}, {1024, 768, 30}};
  return std::make_shared<std::vector<moonlight::DisplayMode>>(displayModes);
}

/**
 * @brief Local state initialization
 */
std::shared_ptr<LocalState>
initialize(const std::string &config_file, const std::string &pkey_filename, const std::string &cert_filename) {
  auto config = load_config(config_file);
  auto display_modes = getDisplayModes();

  x509_st *server_cert;
  evp_pkey_st *server_pkey;
  if (x509::cert_exists(pkey_filename, cert_filename)) {
    server_cert = x509::cert_from_file(cert_filename);
    server_pkey = x509::pkey_from_file(pkey_filename);
  } else {
    logs::log(logs::info, "x509 certificates not present, generating...");
    server_pkey = x509::generate_key();
    server_cert = x509::generate_x509(server_pkey);
    x509::write_to_disk(server_pkey, pkey_filename, server_cert, cert_filename);
  }

  auto pair_cache =
      std::make_shared<std::unordered_map<std::string, PairCache>>(std::unordered_map<std::string, PairCache>());
  LocalState state = {config, display_modes, server_cert, server_pkey, pair_cache};
  return std::make_shared<LocalState>(state);
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

/**
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::trace);
  check_exceptions();

  auto config_file = "config.json";
  auto local_state = initialize(config_file, "key.pem", "cert.pem");

  auto https_server = std::make_unique<HttpsServer>("cert.pem", "key.pem", local_state->config);
  auto http_server = std::make_unique<HttpServer>();

  auto https_thread = HTTPServers::startServer(https_server.get(),
                                               *local_state,
                                               local_state->config->map_port(moonlight::Config::HTTPS_PORT));
  auto http_thread = HTTPServers::startServer(http_server.get(),
                                              *local_state,
                                              local_state->config->map_port(moonlight::Config::HTTP_PORT));

  // Exception and termination handling
  shutdown_handler = [&](int signum) {
    logs::log(logs::info, "Received interrupt signal {}, clean exit", signum);
    if (signum == SIGABRT || signum == SIGSEGV) {
      auto trace_file = "./backtrace.dump";
      logs::log(logs::info, "Dumping stacktrace to {}", trace_file);
      boost::stacktrace::safe_dump_to(trace_file);
    }

    logs::log(logs::info, "Saving back current configuration to file: {}", config_file);
    local_state->config->saveCurrentConfig(config_file);

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