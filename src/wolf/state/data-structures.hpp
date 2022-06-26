#pragma once

#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>
#include <state/config.hpp>
#include <unordered_map>

namespace state {
struct PairCache : state::PairedClient {
  std::string aes_key;

  // Followings will be filled later on during the pair process
  std::optional<std::string> server_secret;
  std::optional<std::string> server_challenge;
  std::optional<std::string> client_hash;
};

struct LocalState {
  const std::shared_ptr<state::JSONConfig> config;
  const std::shared_ptr<std::vector<moonlight::DisplayMode>> display_modes;

  const X509 *server_cert;
  const EVP_PKEY *server_pkey;

  /* Holds temporary results in order to achieve the multistep pairing process */
  const std::shared_ptr<std::unordered_map<std::string, PairCache>> pairing_cache;
};
} // namespace state