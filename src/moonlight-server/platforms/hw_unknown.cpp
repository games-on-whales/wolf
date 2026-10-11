#include "hw.hpp"

std::vector<std::string> linked_devices(std::string_view gpu) {
  return {};
}

GPU_VENDOR get_vendor(std::string_view gpu) {
  return UNKNOWN;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> get_pci_ids(std::string_view gpu) {
  return std::nullopt;
}

std::string get_mac_address(std::string_view local_ip){return "00:00:00:00:00:00"}