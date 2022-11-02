#pragma once

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <crypto/crypto.hpp>
#include <helpers/logger.hpp>
#include <state/data-structures.hpp>

namespace state {
/**
 * @brief Will load a configuration from the given source.
 *
 * If the source is not present, it'll provide some sensible defaults
 */
Config load_or_default(const std::string &source);

/**
 * Side effect, will atomically update the paired clients list in cfg
 */
void pair(const Config &cfg, const PairedClient &client);

/**
 * Side effect, will atomically remove the client from the list of paired clients
 */
void unpair(const Config &cfg, const PairedClient &client);

/**
 * Returns the first PairedClient with the given client_cert
 */
std::optional<PairedClient> get_client_via_ssl(const Config &cfg, x509_st *client_cert) {
  auto paired_clients = cfg.paired_clients.load();
  auto search_result =
      std::find_if(paired_clients->begin(), paired_clients->end(), [&client_cert](const PairedClient &pair_client) {
        auto paired_cert = x509::cert_from_string(pair_client.client_cert);
        auto verification_error = x509::verification_error(paired_cert, client_cert);
        if (verification_error) {
          logs::log(logs::trace, "X509 certificate verification error: {}", verification_error.value());
          return false;
        } else {
          return true;
        }
      });
  if (search_result != paired_clients->end()) {
    return *search_result;
  } else {
    return std::nullopt;
  }
}

/**
 * Returns the first PairedClient with the given client_cert
 */
std::optional<PairedClient> get_client_via_ssl(const Config &cfg, const std::string &client_cert) {
  return get_client_via_ssl(cfg, x509::cert_from_string(client_cert));
}

/**
 * Return the app with the given app_id, throws an exception if not found
 */
immer::box<App> get_app_by_id(const Config &cfg, std::string_view app_id) {
  auto search_result = std::find_if(cfg.apps.begin(), cfg.apps.end(), [&app_id](const state::App &app) {
    return app.base.id == app_id;
  });

  if (search_result != cfg.apps.end())
    return *search_result;
  else
    throw std::runtime_error(fmt::format("Unable to find app with id: {}", app_id));
}

static bool file_exist(const std::string &filename) {
  std::fstream fs(filename);
  return fs.good();
}

static std::string gen_uuid() {
  auto uuid = boost::uuids::random_generator()();
  return boost::lexical_cast<std::string>(uuid);
}
} // namespace state