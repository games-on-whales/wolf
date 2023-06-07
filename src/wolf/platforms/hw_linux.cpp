#include "hw.hpp"
#include <fcntl.h>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <iostream>
#include <optional>
#include <unistd.h>

extern "C" {
#include <pci/pci.h> /* libpci */
#include <xf86drm.h> /* libdrm */
}

/**
 * If the GPU is Nvidia it'll return the correct /dev/nvidiaXX device node
 * This should only return a node when using the proprietary drivers
 *
 * Detection is based on: https://github.com/NVIDIA/open-gpu-kernel-modules/discussions/336#discussioncomment-3262305
 * and helpful hints from @drakulix
 */
std::optional<std::string> get_nvidia_node(std::string_view primary_node) {
  auto paths = utils::split(primary_node, '/');
  auto card_number = paths.back().back();

  auto sys_path = std::filesystem::path(fmt::format("/sys/class/drm/card{}", card_number));
  if (!std::filesystem::exists(sys_path)) {
    logs::log(logs::warning, "{} doesn't exist", sys_path.string());
    return {};
  }

  std::string bus_link;
  try {
    bus_link = std::filesystem::read_symlink(sys_path) //../../devices/pci0000:00/0000:00:01.1/0000:01:00.0/drm/card0
                   .parent_path()                      // /drm/
                   .parent_path()                      // /0000:01:00.0/
                   .filename()                         // 0000:01:00.0
                   .string();
  } catch (std::filesystem::filesystem_error &err) {
    logs::log(logs::warning, "Error while processing {}, {}", sys_path.string(), err.what());
    return {};
  }

  auto nv_information_path = fmt::format("/proc/driver/nvidia/gpus/{}/information", bus_link);
  if (!std::filesystem::exists(nv_information_path)) {
    logs::log(logs::debug, "{} doesn't exists, this might be normal if the GPU is not Nvidia", nv_information_path);
    return {};
  }

  std::ifstream driver_information(nv_information_path);
  if (driver_information.is_open()) {
    std::string line;
    while (std::getline(driver_information, line)) {
      auto line_comp = utils::split(line, ':');
      if (line_comp[0].find("Device Minor") != std::string::npos) {
        std::string device_name = line_comp[1].data();
        device_name.erase(std::remove(device_name.begin(), device_name.end(), ' '), device_name.end());
        device_name.erase(std::remove(device_name.begin(), device_name.end(), '\t'), device_name.end());
        return fmt::format("/dev/nvidia{}", device_name);
      }
    }
  }
  logs::log(logs::warning, "Unable to find 'Device Minor' in {}", nv_information_path);

  return {};
}

/**
 * @return a smart pointer, it'll properly cleanup when going out of scope
 */
std::shared_ptr<drmDevice> drm_open_device(std::string_view device) {
  auto render_node_fd = open(device.data(), O_RDWR | O_CLOEXEC);
  drmDevice *dev = nullptr;
  auto ret = drmGetDevice2(render_node_fd, 0, &dev);
  if (ret < 0) {
    throw std::runtime_error(fmt::format("Error during drmGetDevice for {}, {}", device, strerror(-ret)));
  }

  return {dev, [&render_node_fd](auto dev) {
            drmFreeDevice(&dev);
            close(render_node_fd);
          }};
}

std::vector<std::string> linked_devices(std::string_view gpu) {
  std::vector<std::string> found_devices;

  if (!std::filesystem::exists(gpu)) {
    logs::log(logs::warning, "{} doesn't exists, automatic device recognition failed", gpu);
    return {};
  }
  auto device = drm_open_device(gpu);

  if (device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
    std::string primary_node = device->nodes[DRM_NODE_PRIMARY];
    found_devices.emplace_back(primary_node);
    if (auto nvidia_node = get_nvidia_node(primary_node)) {
      found_devices.emplace_back(nvidia_node.value());

      if (std::filesystem::exists("/dev/nvidia-modeset")) {
        found_devices.emplace_back("/dev/nvidia-modeset");
      }
      if (std::filesystem::exists("/dev/nvidia-uvm")) {
        found_devices.emplace_back("/dev/nvidia-uvm");
      }
      if (std::filesystem::exists("/dev/nvidia-uvm-tools")) {
        found_devices.emplace_back("/dev/nvidia-uvm-tools");
      }
      if (std::filesystem::exists("/dev/nvidiactl")) {
        found_devices.emplace_back("/dev/nvidiactl");
      }
    }
  }

  return found_devices;
}

GPU_VENDOR get_vendor(std::string_view gpu) {
  if (!std::filesystem::exists(gpu)) {
    logs::log(logs::warning, "{} doesn't exists, automatic vendor recognition failed", gpu);
    return UNKNOWN;
  }
  auto device = drm_open_device(gpu);

  pci_access *pacc = pci_alloc();
  pci_init(pacc);
  pci_scan_bus(pacc);
  char devbuf[256];
  std::string vendor_name = pci_lookup_name(pacc,
                                            devbuf,
                                            sizeof(devbuf),
                                            PCI_LOOKUP_VENDOR,
                                            device->deviceinfo.pci->vendor_id,
                                            device->deviceinfo.pci->device_id);
  pci_cleanup(pacc);

  logs::log(logs::debug, "{} vendor: {}", gpu, vendor_name);

  vendor_name = utils::to_lower(vendor_name);
  if (vendor_name.find("nvidia") != std::string::npos) {
    return NVIDIA;
  } else if (vendor_name.find("intel") != std::string::npos) {
    return INTEL;
  } else if (vendor_name.find("amd") != std::string::npos) {
    return AMD;
  }

  logs::log(logs::warning, "Unable to recognise GPU vendor: {}", vendor_name);
  return UNKNOWN;
}