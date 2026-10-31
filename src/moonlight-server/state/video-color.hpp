#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace state {

// Only select formats whose conversion and encoder paths Wolf supports. Unknown
// application-specific caps remain untouched for SDR, and do not advertise HDR.
inline std::optional<std::string> producer_caps_for_client(std::string caps, bool hdr) {
  for (const auto &formats :
       {std::pair{"drm-format=NV12", "drm-format=P010"}, std::pair{"format=NV12", "format=P010_10LE"}}) {
    for (const auto *format : {formats.first, formats.second}) {
      auto pos = caps.find(format);
      if (pos == std::string::npos) {
        continue;
      }
      auto end = pos + std::string_view(format).size();
      if (end != caps.size() && caps[end] != ',' && caps[end] != ' ') {
        continue;
      }
      caps.replace(pos, end - pos, hdr ? formats.second : formats.first);
      return caps;
    }
  }
  return hdr ? std::nullopt : std::optional<std::string>(std::move(caps));
}

inline bool producer_is_hdr(std::string_view caps) {
  return caps.find("drm-format=P010") != std::string_view::npos ||
         caps.find("format=P010_10LE") != std::string_view::npos;
}

// The shared producer cannot yet provide different colour transports to consumers.
inline bool compatible_color_transport(std::string_view first, std::string_view second) {
  return producer_is_hdr(first) == producer_is_hdr(second);
}

inline bool valid_client_color_mode(std::string_view caps, bool hdr, bool hevc) {
  return hdr == producer_is_hdr(caps) && (!hdr || hevc);
}

} // namespace state
