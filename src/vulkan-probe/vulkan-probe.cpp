#include "vulkan-probe.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <helpers/logger.hpp>
#include <optional>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <vector>

#include <gst/vulkan/gstvkdevice.h>
#include <gst/vulkan/gstvkinstance.h>
#include <gst/vulkan/gstvkphysicaldevice.h>

namespace vulkan_probe {

namespace fs = std::filesystem;

/* The extensions gst-vulkan's plugin init gates the encoder registration on, one per
 * codec Wolf knows about. Anything else has no Vulkan encoder to select anyway. */
static std::optional<const char *> extension_for_codec(const std::string &codec) {
  if (codec == "h264") {
    return "VK_KHR_video_encode_h264";
  } else if (codec == "h265") {
    return "VK_KHR_video_encode_h265";
  } else if (codec == "av1") {
    return "VK_KHR_video_encode_av1";
  }
  return std::nullopt;
}

/* The PCI vendor:device IDs of the GPU behind `render_node`, from sysfs. */
struct PciIds {
  uint32_t vendor;
  uint32_t device;
};

static std::optional<PciIds> pci_ids_for_render_node(const std::string &render_node) {
  struct stat st {};
  if (stat(render_node.c_str(), &st) != 0) {
    return std::nullopt;
  }

  /* /dev/dri/renderD129 -> /sys/dev/char/<major>:<minor>/device -> ../../devices/pci.../0000:2d:00.0 */
  auto sysfs_path = fs::path("/sys/dev/char") /
                    (std::to_string(major(st.st_rdev)) + ":" + std::to_string(minor(st.st_rdev))) / "device";

  std::error_code ec;
  auto device_link = fs::read_symlink(sysfs_path, ec);
  if (ec) {
    return std::nullopt;
  }

  auto pci_address = device_link.filename().string();
  auto read_hex = [&pci_address](const char *file) -> std::optional<uint32_t> {
    std::ifstream in(fs::path("/sys/bus/pci/devices") / pci_address / file);
    if (!in.is_open()) {
      return std::nullopt;
    }
    uint32_t value = 0;
    in >> std::hex >> value;
    return value;
  };

  auto vendor = read_hex("vendor");
  auto device = read_hex("device");
  if (!vendor || !device) {
    return std::nullopt;
  }
  return PciIds{*vendor, *device};
}

/* One Vulkan instance for the whole process, created on first use and never destroyed:
 * like gst-wayland-display's keep_vulkan_drivers_loaded(), unloading the drivers between
 * uses breaks NVIDIA (it calls eglTerminate on a display other components still use) and
 * reloading them races other threads enumerating extensions. */
static GstVulkanInstance *shared_instance() {
  static GstVulkanInstance *instance = [] {
    auto *instance = gst_vulkan_instance_new();
    GError *error = nullptr;
    if (!gst_vulkan_instance_open(instance, &error)) {
      logs::log(logs::debug, "Vulkan not available: {}", error ? error->message : "unknown error");
      g_clear_error(&error);
      gst_object_unref(instance);
      instance = nullptr;
    }
    return instance;
  }();
  return instance;
}

/* VCN 1.0 parts (the Raven, Raven2 and Picasso APUs, and Vega 20): RADV
 * advertises the video encode extensions on them, but its encode path only
 * supports VCN 2.0+ and these chips emit corrupt bitstreams. Skip them so the
 * VA-API encoders take over instead. */
static bool is_known_broken_encoder(const VkPhysicalDeviceProperties &props) {
  std::string name = props.deviceName;
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);
  for (const char *chip : {"gfx901", "gfx902", "gfx903", "gfx906", "raven", "picasso"}) {
    if (name.find(chip) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool supports_video_encode(std::string_view render_node, std::string_view codec) {
  std::string codec_str{codec};
  std::string node_str{render_node};
  auto extension = extension_for_codec(codec_str);
  if (!extension) {
    logs::log(logs::warning, "No Vulkan encoder extension for codec {}", codec_str);
    return false;
  }

  auto pci_ids = pci_ids_for_render_node(node_str);
  if (!pci_ids) {
    logs::log(logs::debug, "Cannot resolve the GPU behind {}, skipping the Vulkan encoders", node_str);
    return false;
  }

  auto *instance = shared_instance();
  if (!instance) {
    return false;
  }

  /* Match the physical device by PCI vendor:device IDs. Two identical GPUs share the
   * IDs, but they also share the codec support, so the answer is the same either way. */
  for (guint i = 0; i < instance->n_physical_devices; i++) {
    GstVulkanPhysicalDevice *physical = gst_vulkan_physical_device_new(instance, i);
    bool matches = physical->properties.vendorID == pci_ids->vendor && physical->properties.deviceID == pci_ids->device;
    std::string device_name = matches ? std::string(physical->properties.deviceName) : "";
    bool broken = matches && is_known_broken_encoder(physical->properties);
    gst_object_unref(physical);
    if (!matches) {
      continue;
    }
    if (broken) {
      logs::log(logs::info,
                "Skipping the Vulkan encoders on {}: {} cannot encode correctly "
                "(VCN 1.0, falling back to the other encoders)",
                node_str,
                device_name);
      return false;
    }

    /* The same check gstvulkan.c's plugin init runs before registering the encoders:
     * constructing the device enables gst-vulkan's optional extensions, which succeeds
     * only when the physical device supports them. */
    GstVulkanDevice *device = gst_vulkan_device_new_with_index(instance, i);
    bool supported = gst_vulkan_device_is_extension_enabled(device, *extension);
    gst_object_unref(device);

    logs::log(logs::debug,
              "Vulkan {} encode on {} (physical device {}): {}",
              codec_str,
              node_str,
              i,
              supported ? "supported" : "not supported");
    return supported;
  }

  logs::log(logs::debug, "No Vulkan physical device matches the GPU behind {}", node_str);
  return false;
}

} // namespace vulkan_probe
