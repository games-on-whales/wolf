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
 * @brief Based only on the client_id returns True if there's already a paired client in Config.
 *
 * Bear in mind that even if the HTTP endpoint can return paired == true just by looking at the client_id,
 * on the HTTPS endpoint we'll also check the actual provided certificate to see if it's valid; see: HTTPSCustomCert
 */
bool is_paired(const Config &cfg, const std::string &client_id);

/**
 * Side effect, will atomically update the paired clients list in cfg
 */
void pair(const Config &cfg, const PairedClient &client);

/**
 * Returns the first PairedClient with the given client_id
 */
std::optional<PairedClient> find_by_id(const Config &cfg, const std::string &client_id);
} // namespace state