#pragma once
#include <string>

namespace moonlight {

struct DisplayMode {
  const int width;
  const int height;
  const int refreshRate;
};

struct PairedClients {
  const std::string client_id;
  const std::string client_cert;
};

} // namespace moonlight