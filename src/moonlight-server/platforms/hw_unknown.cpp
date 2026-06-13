#include "hw.hpp"

#include <filesystem>

std::vector<std::string> linked_devices(std::string_view gpu) {
  return {};
}

std::string get_render_node_name(std::string_view render_node) {
  return std::filesystem::path(render_node).filename().string();
}

GPU_VENDOR get_vendor(std::string_view gpu) {
  return UNKNOWN;
}

std::string get_vendor_name(GPU_VENDOR vendor) {
  return "Unknown";
}

std::string get_mac_address(std::string_view local_ip) {
  return "00:00:00:00:00:00";
}
