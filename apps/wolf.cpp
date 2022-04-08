#include <vector>

#include <data-structures.hpp>
#include <helpers/config.hpp>
#include <helpers/logger.cpp>
#include <moonlight/data-structures.hpp>
#include <servers.cpp>

/**
 * @brief Will try to load the config file and fallback to defaults
 */
std::shared_ptr<Config> load_config(const std::string config_file) {
  logs::log(logs::info, "Reading config file from: {}", config_file);
  try {
    return std::make_shared<Config>(config_file);
  } catch (const std::exception &e) {
    logs::log(logs::warning, "Unable to open config file: {}, using defaults", config_file);
    pt::ptree defaults;

    defaults.put("base_port", 47989);
    // TODO: external_ip, local_ip, mac_address

    return std::make_shared<Config>(defaults);
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
std::shared_ptr<LocalState> initialize(const std::string config_file, const std::string cert_filename) {
  auto config = load_config(config_file);
  auto display_modes = getDisplayModes();
  auto server_cert = x509::cert_from_file(cert_filename);
  LocalState state = {config, display_modes, server_cert, std::make_shared<std::vector<PairCache>>()};
  return std::make_shared<LocalState>(state);
}

/**
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::trace);

  auto https_server = HTTPServers::createHTTPS("key.pem", "cert.pem");
  auto http_server = HTTPServers::createHTTP();

  auto config_file = "config.json";
  auto local_state = initialize(config_file, "cert.pem");

  auto https_thread = HTTPServers::startServer(https_server.get(),
                                               *local_state,
                                               local_state->config->map_port(Config::HTTPS_PORT));
  auto http_thread =
      HTTPServers::startServer(http_server.get(), *local_state, local_state->config->map_port(Config::HTTP_PORT));

  https_thread.join();
  http_thread.join();

  // TODO: clean exit

  logs::log(logs::info, "Saving back current configuration to file: {}", config_file);
  local_state->config->saveCurrentConfig(config_file);
  return 0;
}