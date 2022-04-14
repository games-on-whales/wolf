#include <vector>

#include <helpers/logger.hpp>
#include <moonlight/config.hpp>
#include <moonlight/data-structures.hpp>
#include <rest/servers.cpp>

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
 * @brief here's where the magic starts
 */
int main(int argc, char *argv[]) {
  logs::init(logs::trace);

  auto config_file = "config.json";
  auto local_state = initialize(config_file, "key.pem", "cert.pem");

  auto https_server = HTTPServers::createHTTPS("key.pem", "cert.pem", local_state->config);
  auto http_server = HTTPServers::createHTTP();

  auto https_thread = HTTPServers::startServer(https_server.get(),
                                               *local_state,
                                               local_state->config->map_port(moonlight::Config::HTTPS_PORT));
  auto http_thread = HTTPServers::startServer(http_server.get(),
                                              *local_state,
                                              local_state->config->map_port(moonlight::Config::HTTP_PORT));

  https_thread.join();
  http_thread.join();

  // TODO: clean exit

  logs::log(logs::info, "Saving back current configuration to file: {}", config_file);
  local_state->config->saveCurrentConfig(config_file);
  return 0;
}