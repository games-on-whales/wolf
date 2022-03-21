#pragma once

#include <helpers/config.hpp>
#include <moonlight/user-pair.hpp>
#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>

struct LocalState {
  const std::shared_ptr<Config> config;
  const std::shared_ptr<moonlight::UserPair> pair_handler;
  const std::shared_ptr<std::vector<moonlight::DisplayMode>> display_modes;
  const X509* server_cert;
};