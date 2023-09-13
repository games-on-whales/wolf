#include "hw.hpp"

std::vector<std::string> linked_devices(std::string_view gpu) {
  return {};
}

GPU_VENDOR get_vendor(std::string_view gpu) {
  return UNKNOWN;
}

std::string get_mac_address(std::string_view local_ip) {
  return "00:00:00:00:00:00"
}