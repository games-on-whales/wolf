#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * Given a /dev/dri/ node will return all the other paths that are linked to it (if any)
 *
 * Particularly useful for Nvidia where given a render-node (ex: /dev/dri/renderD128)
 * it will return the corresponding primary node (ex: /dev/dri/card0)
 * plus all the required /dev/nvidia* devices
 */
std::vector<std::string> linked_devices(std::string_view gpu);

enum GPU_VENDOR {
  NVIDIA,
  AMD,
  INTEL,
  UNKNOWN
};

GPU_VENDOR get_vendor(std::string_view gpu);

std::string get_mac_address(std::string_view local_ip);
