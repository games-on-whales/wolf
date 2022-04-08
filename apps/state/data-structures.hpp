#pragma once

#include <helpers/config.hpp>
#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>

struct PairCache : moonlight::PairedClients {
  const std::string aes_key;
};

struct LocalState {
  const std::shared_ptr<Config> config;
  const std::shared_ptr<std::vector<moonlight::DisplayMode>> display_modes;

  const X509 *server_cert;

  /* Holds pairs of client_id/client_cert that are in the middle of the pairing process*/
  const std::shared_ptr<std::vector<PairCache>> pairing_cache;
};