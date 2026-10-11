// SPDX-License-Identifier: GPL-3.0-or-later
//
// Process-wide PyroWave device, dma-buf import cache and GStreamer plugin registration.
//
// PyroWave keeps one Vulkan device per process: every encoder shares it, so this header owns the
// device lifetime and hands out shared references. It is only usable when Wolf is built with
// WOLF_PYROWAVE (libpyrowave-shared + Vulkan headers found at configure time).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <vulkan/vulkan_core.h>

#include <pyrowave.h>

#include "gst-plugin/pyrowave/pyrowave_align.hpp"

namespace wolf::pyrowave {

/**
 * RAII owner of the process-wide pyrowave device.
 *
 * Never construct one directly: use acquire_device(), which reuses the live device when there is one
 * and creates it otherwise. Every encoder holds a shared reference for as long as it exists, so the
 * device outlives all of them.
 */
class Device {
public:
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&) = delete;
  Device &operator=(Device &&) = delete;

  /** Destroys the underlying pyrowave device. */
  ~Device();

  /** @return The raw handle to pass to the pyrowave encoder/packetizer entry points. */
  [[nodiscard]] pyrowave_device get() const noexcept {
    return device_;
  }

private:
  friend std::shared_ptr<Device> acquire_device();

  /**
   * Takes ownership of an already created device.
   *
   * @param device A device created by one of the pyrowave_create_device* entry points.
   */
  explicit Device(pyrowave_device device) : device_(device) {}

  pyrowave_device device_;
};

/**
 * Returns the process-wide PyroWave device, creating it on first use.
 *
 * The device is selected from the GPU behind WOLF_ENCODER_NODE (falling back to WOLF_RENDER_NODE and
 * then /dev/dri/renderD128) by matching its PCI ids, so that on multi-GPU hosts encoding happens on
 * the same GPU as the compositor. Encoding runs on an async compute queue.
 *
 * @return A shared device, or nullptr when no usable Vulkan device is present. The device is
 * destroyed as soon as the last reference goes away, which is what allows a later call to retry.
 */
std::shared_ptr<Device> acquire_device();

/**
 * Checks that a PyroWave device can be created on this host.
 *
 * Used as the availability gate before advertising the codec: it creates a device and immediately
 * destroys it.
 *
 * @return true when a device could be created.
 */
bool probe();

/**
 * Registers the "pyrowave" GStreamer plugin, exposing the pyrowaveenc element.
 *
 * Wolf's encoder availability check looks a plugin up by name in the GStreamer registry, so the
 * element is registered through a static plugin rather than as a bare element. Does nothing when
 * probe() fails, which is how a host without a usable Vulkan device ends up without PyroWave.
 */
void register_gst_plugin();

/** Number of dma-buf planes this implementation can describe. */
constexpr std::size_t MAX_DMABUF_PLANES = 4;

/**
 * Geometry of one plane of a dma-buf, as reported by the negotiated caps and the buffer's video
 * meta.
 */
struct DmaBufPlane {
  std::size_t offset; /**< Byte offset of the plane inside the dma-buf. */
  std::size_t pitch;  /**< Row pitch of the plane in bytes. */
};

/**
 * Everything needed to import a compositor dma-buf as a Vulkan image for PyroWave.
 *
 * Deliberately trivially copyable and allocation free: it is rebuilt for every frame, and only the
 * first frame of each buffer in the compositor's pool actually reaches Vulkan.
 */
struct DmaBufImage {
  int fd{};                 /**< dma-buf file descriptor; the importer dups it and never takes it. */
  std::uint32_t fourcc{};   /**< DRM_FORMAT_* of the buffer, from the caps. */
  std::uint64_t modifier{}; /**< DRM format modifier, from the caps. */
  std::uint32_t width{};    /**< Luma width of the buffer in pixels. */
  std::uint32_t height{};   /**< Luma height of the buffer in pixels. */
  std::size_t num_planes{}; /**< Number of valid entries in `planes`. */
  std::array<DmaBufPlane, MAX_DMABUF_PLANES> planes{};

  /**
   * Compares the buffer geometry, ignoring the file descriptor.
   *
   * Two dma-bufs from the same pool share an inode but not necessarily the same fd, and a pooled
   * buffer can be reallocated with a different geometry, so both the inode and the geometry take
   * part in the cache key.
   *
   * @param other Buffer description to compare against.
   * @return true when both descriptions have the same format, size and plane layout.
   */
  [[nodiscard]] bool same_layout(const DmaBufImage &other) const noexcept;
};

/**
 * An imported dma-buf and the image view to encode from it.
 *
 * Both handles are owned by the importer's cache: they stay valid until the next import_dma_buf()
 * or clear() call on the same importer.
 */
struct ImportedImage {
  pyrowave_image image{};     /**< Handle for the acquire/release ownership barriers. */
  pyrowave_image_view view{}; /**< View to hand to the scaled encode entry point. */
};

/**
 * Maps a DRM fourcc to the Vulkan format PyroWave expects for it.
 *
 * Only the formats a compositor realistically hands over (8-bit 4:2:0 NV12 and the two common
 * RGB(A) layouts) are recognized; everything else is reported as unsupported.
 *
 * @param fourcc DRM_FORMAT_* value.
 * @return The matching VkFormat, or std::nullopt when PyroWave cannot consume it.
 */
std::optional<VkFormat> drm_format_to_vk_format(std::uint32_t fourcc);

/**
 * @param fourcc DRM_FORMAT_* value.
 * @return true when the format is planar (multi-plane), which requires mutable format views.
 */
bool drm_format_is_planar(std::uint32_t fourcc);

/** @return true when the fourcc is one of the NV12 layouts PyroWave can colour convert itself. */
bool drm_format_is_nv12(std::uint32_t fourcc);

/**
 * @param modifier DRM format modifier from the caps.
 * @return true when the modifier describes a plain linear layout, which is what a CPU-mapped
 * dma-buf always looks like.
 */
bool drm_modifier_is_linear(std::uint64_t modifier);

/**
 * Imports compositor dma-bufs into Vulkan images, caching the result.
 *
 * The compositor cycles through a small pool of buffers, and Vulkan image creation plus memory
 * import is far too expensive to repeat per frame (it also stalls the driver), so imports are keyed
 * on the dma-buf inode and kept in a small least-recently-used cache. A buffer that the compositor
 * reallocates with a different geometry is re-imported.
 */
class DmaBufImporter {
public:
  /** Number of distinct dma-bufs kept imported at once. */
  static constexpr std::size_t CACHE_SIZE = 8;

  /**
   * @param device Device the images are imported into; must stay alive for the importer's lifetime.
   */
  explicit DmaBufImporter(std::shared_ptr<Device> device);

  DmaBufImporter(const DmaBufImporter &) = delete;
  DmaBufImporter &operator=(const DmaBufImporter &) = delete;

  /** Destroys every cached image. */
  ~DmaBufImporter();

  /**
   * Imports a dma-buf, or returns the cached import of the same buffer.
   *
   * @param image Buffer to import; `fd` is duplicated, never taken over.
   * @return The imported image and its encode view, or std::nullopt when the format is unsupported
   * or the driver refused the import (in which case the caller falls back to the CPU path). A
   * failure is logged once per reason to keep a broken pipeline from flooding the log.
   */
  std::optional<ImportedImage> import_dma_buf(const DmaBufImage &image);

  /** Destroys every cached image, for example because the negotiated caps changed. */
  void clear();

private:
  /**
   * One cache slot: the key (dma-buf inode), the geometry it was imported with, and the handles.
   */
  struct Entry {
    std::uint64_t inode{};     /**< Key: st_ino of the dma-buf. */
    std::uint64_t device{};    /**< Key: st_dev of the dma-buf. */
    DmaBufImage image{};       /**< Geometry the entry was imported with. */
    pyrowave_image imported{}; /**< Imported Vulkan image, or nullptr for an unused slot. */
    pyrowave_image_view view{};
  };

  /**
   * Creates the Vulkan image and view for a dma-buf that is not in the cache.
   *
   * @param image Buffer to import.
   * @return The populated entry, or std::nullopt when the driver refused the import.
   */
  std::optional<Entry> create_entry(const DmaBufImage &image);

  /** Destroys the image held by `entry` and marks the slot as unused. */
  void destroy_entry(Entry &entry) noexcept;

  std::shared_ptr<Device> device_;
  std::array<Entry, CACHE_SIZE> entries_{}; /**< MRU first, unused slots last. */
  std::size_t entry_count_{};
  bool warned_unsupported_format_{};
  bool warned_import_failure_{};
};

} // namespace wolf::pyrowave
