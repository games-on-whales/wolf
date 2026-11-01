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

/**
 * Given /dev/dri/renderD128 returns renderD128
 * works with symlinks too, /dev/dri/by-path/pci-0000:2d:00.0-render returns renderD128
 */
std::string get_render_node_name(std::string_view render_node);

enum GPU_VENDOR {
  NVIDIA,
  AMD,
  INTEL,
  UNKNOWN
};

GPU_VENDOR get_vendor(std::string_view gpu);

std::string get_vendor_name(GPU_VENDOR vendor);

std::string get_mac_address(std::string_view local_ip);
