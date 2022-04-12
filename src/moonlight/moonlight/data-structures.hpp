#pragma once
#include <string>

namespace moonlight {

struct DisplayMode {
  const int width;
  const int height;
  const int refreshRate;
};

struct PairedClients {
  std::string client_id;
  std::string client_cert;
};

} // namespace moonlight