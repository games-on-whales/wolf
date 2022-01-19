#pragma once

#include <helpers/config.hpp>
#include <moonlight/user-pair.hpp>

struct LocalState {
  const std::shared_ptr<Config> config;
  const std::shared_ptr<moonlight::UserPair> pair_handler;
  const std::shared_ptr<std::vector<moonlight::DisplayMode>> display_modes;
};