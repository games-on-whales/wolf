#pragma once

#include <state/data-structures.hpp>

namespace state {
/**
 * @brief Will load a configuration from the given source.
 *
 * If the source is not present, it'll provide some sensible defaults
 */
template <class S> Config load_or_default(const S &source);

/**
 * Will store back the given configuration into dest
 */
template <class S> void save(const Config &cfg, const S &dest);

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
std::optional<PairedClient> get_client_via_ssl(const Config &cfg, x509_st *client_cert);

/**
 * Returns the first PairedClient with the given client_cert
 */
std::optional<PairedClient> get_client_via_ssl(const Config &cfg, const std::string &client_cert) {
  return get_client_via_ssl(cfg, x509::cert_from_string(client_cert));
}
} // namespace state