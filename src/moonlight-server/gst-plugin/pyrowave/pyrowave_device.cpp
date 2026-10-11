// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the process-wide PyroWave device, the dma-buf import cache and the GStreamer
// plugin registration. See pyrowave_device.hpp for the API and its invariants.

#include "gst-plugin/pyrowave/pyrowave_device.hpp"

#include "gst-plugin/pyrowave/gstpyrowaveenc.hpp"

#include <algorithm>
#include <array>
#include <drm_fourcc.h>
#include <fmt/format.h>
#include <gst/gst.h>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <mutex>
#include <platforms/hw.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace wolf::pyrowave {

namespace {

/**
 * Name of the GStreamer plugin hosting the pyrowaveenc element.
 *
 * Wolf's encoder availability check looks a plugin up by this name in the GStreamer registry, which
 * is why the element is registered through a plugin instead of directly.
 */
constexpr const char *PLUGIN_NAME = "pyrowave";

/** Name of the GStreamer element exposing the encoder. */
constexpr const char *ELEMENT_NAME = "pyrowaveenc";

/**
 * Process-wide device cache.
 *
 * Function local so that nothing outside this translation unit can reach it, and holding a weak
 * reference so the device is torn down as soon as the last encoder that uses it is destroyed.
 */
struct DeviceCache {
  std::mutex mutex;
  std::weak_ptr<Device> device;
};

/**
 * @return The process-wide device cache.
 */
DeviceCache &device_cache() {
  static DeviceCache cache;
  return cache;
}

/**
 * GStreamer plugin initializer, called by the registry when the static plugin is first needed.
 *
 * @param plugin The plugin being initialized.
 * @return true when the encoder element was registered.
 */
gboolean plugin_init(GstPlugin *plugin) {
  return gst_element_register(plugin, ELEMENT_NAME, GST_RANK_PRIMARY, gst_TYPE_pyrowave_enc);
}

/**
 * Logs at warning level once, then stays quiet.
 *
 * A pipeline that cannot use the zero-copy path hits the same failure for every frame; logging it
 * once keeps the log readable while still telling the operator what happened.
 *
 * @param warned Flag that records whether the message was already emitted.
 * @param message Message to log.
 */
void warn_once(bool &warned, const std::string &message) {
  if (!warned) {
    warned = true;
    logs::log(logs::warning, "{}", message);
  }
}

} // namespace

Device::~Device() {
  if (device_ != nullptr) {
    pyrowave_device_destroy(device_);
  }
}

std::shared_ptr<Device> acquire_device() {
  auto &cache = device_cache();
  std::lock_guard lock(cache.mutex);

  if (auto device = cache.device.lock()) {
    return device;
  }

  // WOLF_ENCODER_NODE wins over WOLF_RENDER_NODE, the same resolution configTOML uses to pick the
  // node the encoder itself runs on.
  auto render_node = utils::get_env("WOLF_RENDER_NODE", "/dev/dri/renderD128");
  auto encoder_node = utils::get_env("WOLF_ENCODER_NODE", render_node);

  pyrowave_device raw_device = nullptr;
  auto pci_ids = get_pci_ids(encoder_node);
  if (pci_ids) {
    auto result = pyrowave_create_device_by_compat(pci_ids->first,
                                                   pci_ids->second,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   VK_QUEUE_GLOBAL_PRIORITY_MEDIUM,
                                                   &raw_device);
    if (result == PYROWAVE_SUCCESS) {
      logs::log(logs::debug,
                "PyroWave: using the device at {} (PCI {:04x}:{:04x})",
                encoder_node,
                pci_ids->first,
                pci_ids->second);
    } else {
      logs::log(logs::debug,
                "PyroWave: no Vulkan device matching {} (PCI {:04x}:{:04x}), falling back to the default device",
                encoder_node,
                pci_ids->first,
                pci_ids->second);
      raw_device = nullptr;
    }
  }

  if (raw_device == nullptr) {
    auto result = pyrowave_create_default_device(&raw_device);
    if (result != PYROWAVE_SUCCESS || raw_device == nullptr) {
      logs::log(logs::warning, "PyroWave: unable to create a Vulkan device ({})", (int)result);
      return nullptr;
    }
  }

  // Encode on an async compute queue so it does not contend with the compositor's graphics queue.
  if (auto result = pyrowave_device_set_queue_type(raw_device, VK_QUEUE_COMPUTE_BIT); result != PYROWAVE_SUCCESS) {
    logs::log(logs::debug, "PyroWave: compute queue unavailable ({}), falling back to the graphics queue", (int)result);
  }

  auto device = std::shared_ptr<Device>(new Device(raw_device));
  cache.device = device;
  return device;
}

bool probe() {
  return acquire_device() != nullptr;
}

void register_gst_plugin() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  std::uint32_t major = 0, minor = 0, patch = 0;
  pyrowave_get_api_version(&major, &minor, &patch);
  if (major != PYROWAVE_API_VERSION_MAJOR || minor != PYROWAVE_API_VERSION_MINOR) {
    logs::log(logs::warning,
              "PyroWave: runtime library is {}.{}.{}, built against {}.{}.{}; continuing anyway",
              major,
              minor,
              patch,
              PYROWAVE_API_VERSION_MAJOR,
              PYROWAVE_API_VERSION_MINOR,
              PYROWAVE_API_VERSION_PATCH);
  }

  if (!probe()) {
    logs::log(logs::info, "PyroWave: no usable Vulkan device, codec disabled");
    return;
  }

  if (!gst_plugin_register_static(GST_VERSION_MAJOR,
                                  GST_VERSION_MINOR,
                                  PLUGIN_NAME,
                                  "PyroWave GPU wavelet encoder",
                                  plugin_init,
                                  "1.0",
                                  "LGPL",
                                  "wolf",
                                  "wolf",
                                  "https://github.com/games-on-whales/wolf/")) {
    logs::log(logs::warning, "PyroWave: unable to register the {} GStreamer plugin", PLUGIN_NAME);
  }
}

bool DmaBufImage::same_layout(const DmaBufImage &other) const noexcept {
  if (fourcc != other.fourcc || modifier != other.modifier || width != other.width || height != other.height ||
      num_planes != other.num_planes) {
    return false;
  }

  for (std::size_t i = 0; i < num_planes; i++) {
    if (planes[i].offset != other.planes[i].offset || planes[i].pitch != other.planes[i].pitch) {
      return false;
    }
  }

  return true;
}

std::optional<VkFormat> drm_format_to_vk_format(std::uint32_t fourcc) {
  switch (fourcc) {
  case DRM_FORMAT_XRGB8888:
  case DRM_FORMAT_ARGB8888:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case DRM_FORMAT_XBGR8888:
  case DRM_FORMAT_ABGR8888:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case DRM_FORMAT_NV12:
    return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
  default:
    return std::nullopt;
  }
}

bool drm_format_is_planar(std::uint32_t fourcc) {
  return fourcc == DRM_FORMAT_NV12;
}

bool drm_format_is_nv12(std::uint32_t fourcc) {
  return fourcc == DRM_FORMAT_NV12;
}

bool drm_modifier_is_linear(std::uint64_t modifier) {
  return modifier == DRM_FORMAT_MOD_LINEAR || modifier == DRM_FORMAT_MOD_INVALID;
}

DmaBufImporter::DmaBufImporter(std::shared_ptr<Device> device) : device_(std::move(device)) {}

DmaBufImporter::~DmaBufImporter() {
  clear();
}

void DmaBufImporter::destroy_entry(Entry &entry) noexcept {
  if (entry.imported != nullptr) {
    pyrowave_image_destroy(entry.imported);
    entry.imported = nullptr;
  }
  entry.view = {};
  entry.inode = 0;
  entry.device = 0;
}

void DmaBufImporter::clear() {
  for (auto &entry : entries_) {
    destroy_entry(entry);
  }
  entry_count_ = 0;
}

std::optional<DmaBufImporter::Entry> DmaBufImporter::create_entry(const DmaBufImage &image) {
  auto vk_format = drm_format_to_vk_format(image.fourcc);
  if (!vk_format) {
    warn_once(warned_unsupported_format_,
              fmt::format("PyroWave: cannot import dma-buf format 0x{:08x}, use the CPU path", image.fourcc));
    return std::nullopt;
  }

  if (image.num_planes == 0 || image.num_planes > MAX_DMABUF_PLANES) {
    warn_once(warned_import_failure_, fmt::format("PyroWave: unsupported dma-buf plane count {}", image.num_planes));
    return std::nullopt;
  }

  // The import takes ownership of the file descriptor, so hand over a duplicate and keep the
  // caller's.
  const int fd = ::dup(image.fd);
  if (fd < 0) {
    warn_once(warned_import_failure_, fmt::format("PyroWave: cannot duplicate dma-buf fd {}", image.fd));
    return std::nullopt;
  }

  const bool planar = drm_format_is_planar(image.fourcc);
  // A dma-buf that does not advertise a modifier is linear by definition.
  const std::uint64_t modifier = drm_modifier_is_linear(image.modifier) ? DRM_FORMAT_MOD_LINEAR : image.modifier;

  std::array<VkSubresourceLayout, MAX_DMABUF_PLANES> plane_layouts{};
  for (std::size_t i = 0; i < image.num_planes; i++) {
    plane_layouts[i].offset = image.planes[i].offset;
    plane_layouts[i].rowPitch = image.planes[i].pitch;
    // size, depthPitch and arrayPitch must be zero when importing.
  }

  // The scaler reads planar buffers through per-plane views, which requires a mutable format image.
  constexpr std::array<VkFormat, 2> PLANAR_VIEW_FORMATS{VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM};
  VkImageFormatListCreateInfo format_list{};
  format_list.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
  format_list.viewFormatCount = planar ? PLANAR_VIEW_FORMATS.size() : 0;
  format_list.pViewFormats = PLANAR_VIEW_FORMATS.data();

  VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info{};
  modifier_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
  modifier_info.drmFormatModifier = modifier;
  modifier_info.drmFormatModifierPlaneCount = image.num_planes;
  modifier_info.pPlaneLayouts = plane_layouts.data();
  modifier_info.pNext = planar ? &format_list : nullptr;

  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.pNext = &modifier_info;
  image_info.flags = planar ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = *vk_format;
  image_info.extent = {image.width, image.height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  pyrowave_image_create_info create_info{};
  create_info.device = device_->get();
  create_info.external_handle = static_cast<pyrowave_os_handle>(fd);
  create_info.handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  create_info.image_create_info = &image_info;

  Entry entry{};
  entry.image = image;
  auto result = pyrowave_image_create(&create_info, &entry.imported);
  if (result != PYROWAVE_SUCCESS || entry.imported == nullptr) {
    // On a failed import the file descriptor is still ours.
    ::close(fd);
    entry.imported = nullptr;
    warn_once(warned_import_failure_,
              fmt::format("PyroWave: cannot import dma-buf (drm-format 0x{:08x}:0x{:016x}, {}x{}, {} planes): "
                          "error {}, set WOLF_USE_ZERO_COPY=FALSE to use the CPU path",
                          image.fourcc,
                          modifier,
                          image.width,
                          image.height,
                          image.num_planes,
                          (int)result));
    return std::nullopt;
  }

  // Color aspect keeps the NV12 layout (the scaling path special cases it) and gives the sRGB-free
  // UNORM view for the RGB formats.
  result =
      pyrowave_image_get_image_view(entry.imported, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, &entry.view);
  if (result != PYROWAVE_SUCCESS) {
    pyrowave_image_destroy(entry.imported);
    entry.imported = nullptr;
    warn_once(warned_import_failure_,
              fmt::format("PyroWave: cannot create an image view for drm-format 0x{:08x} (error {})",
                          image.fourcc,
                          (int)result));
    return std::nullopt;
  }

  return entry;
}

std::optional<ImportedImage> DmaBufImporter::import_dma_buf(const DmaBufImage &image) {
  struct stat buf{};
  if (::fstat(image.fd, &buf) != 0) {
    warn_once(warned_import_failure_, fmt::format("PyroWave: cannot stat dma-buf fd {}", image.fd));
    return std::nullopt;
  }

  // Move-to-front cache: reuse the import of a buffer the compositor is recycling.
  for (std::size_t i = 0; i < entry_count_; i++) {
    auto &entry = entries_[i];
    if (entry.inode != static_cast<std::uint64_t>(buf.st_ino) ||
        entry.device != static_cast<std::uint64_t>(buf.st_dev) || !entry.image.same_layout(image)) {
      continue;
    }

    std::rotate(entries_.begin(), entries_.begin() + i, entries_.begin() + i + 1);

    ImportedImage imported{};
    imported.image = entries_[0].imported;
    imported.view = entries_[0].view;
    return imported;
  }

  if (entry_count_ == CACHE_SIZE) {
    destroy_entry(entries_[CACHE_SIZE - 1]);
    entry_count_--;
  }

  auto entry = create_entry(image);
  if (!entry) {
    return std::nullopt;
  }
  entry->inode = static_cast<std::uint64_t>(buf.st_ino);
  entry->device = static_cast<std::uint64_t>(buf.st_dev);

  std::rotate(entries_.begin(), entries_.begin() + entry_count_, entries_.begin() + entry_count_ + 1);
  entries_[0] = *entry;
  entry_count_++;

  ImportedImage imported{};
  imported.image = entries_[0].imported;
  imported.view = entries_[0].view;
  return imported;
}

} // namespace wolf::pyrowave
