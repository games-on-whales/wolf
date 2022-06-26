#pragma once
#include <string>

namespace moonlight {

struct DisplayMode {
  const int width;
  const int height;
  const int refreshRate;
};

struct App {
  const std::string title;
  const std::string id;
  const bool support_hdr;
  // TODO: launch command or something similar
  // TODO: icon
};

} // namespace moonlight