#pragma once

#include <gst/gst.h>
#include <string>
#include <string_view>

namespace state {
// Select storage from the immutable admitted formats on every launch/resume.
// The DRM modifier is part of the import contract: plain NV12/P010 means linear.
inline std::string producer_caps_for_hdr(std::string caps, bool hdr_capable) {
  if (caps.find("memory:VulkanImage") != std::string::npos) {
    // The Vulkan producer writes encoder-ready pixels. P010 alone only
    // selects storage: without the PQ colour contract an SDR-only launcher
    // uses the SDR matrix, then the HDR connection misinterprets those pixels.
    return hdr_capable ? "video/x-raw(memory:VulkanImage), format=P010_10LE, colorimetry=bt2100-pq"
                       : "video/x-raw(memory:VulkanImage), format=NV12";
  }
  if (caps.find("memory:DMABuf") == std::string::npos)
    return caps;

  GstCaps *source = gst_caps_from_string(caps.c_str());
  if (!source)
    return "EMPTY";
  GstCaps *selected = gst_caps_new_empty();
  const std::string_view requested_format = hdr_capable ? "P010" : "NV12";
  const auto matches_storage = [requested_format](const GValue *value) {
    if (!G_VALUE_HOLDS_STRING(value))
      return false;
    const auto text = g_value_get_string(value);
    if (!text)
      return false;
    const std::string_view format{text};
    return format == requested_format ||
           (format.size() > 5 && format.substr(0, 4) == requested_format && format[4] == ':');
  };
  for (guint i = 0; i < gst_caps_get_size(source); ++i) {
    const auto features = gst_caps_get_features(source, i);
    const auto structure = gst_caps_get_structure(source, i);
    const auto formats = gst_structure_get_value(structure, "drm-format");
    if (!gst_caps_features_contains(features, "memory:DMABuf") || !formats)
      continue;
    GValue retained = G_VALUE_INIT;
    g_value_init(&retained, GST_TYPE_LIST);
    if (GST_VALUE_HOLDS_LIST(formats)) {
      for (guint n = 0; n < gst_value_list_get_size(formats); ++n) {
        const auto value = gst_value_list_get_value(formats, n);
        if (matches_storage(value))
          gst_value_list_append_value(&retained, value);
      }
    } else if (matches_storage(formats)) {
      gst_value_list_append_value(&retained, formats);
    }
    if (gst_value_list_get_size(&retained) > 0) {
      auto copy = gst_structure_copy(structure);
      gst_structure_set_value(copy, "drm-format", &retained);
      // P010 selects storage, not its transfer function. Keep PQ explicit when
      // a generic DMA-BUF advertisement is selected again during app handoff.
      if (hdr_capable)
        gst_structure_set(copy, "colorimetry", G_TYPE_STRING, "bt2100-pq", nullptr);
      gst_caps_append_structure_full(selected, copy, gst_caps_features_copy(features));
    }
    g_value_unset(&retained);
  }
  gchar *serialized = gst_caps_to_string(selected);
  std::string result{serialized};
  g_free(serialized);
  gst_caps_unref(selected);
  gst_caps_unref(source);
  return result;
}
} // namespace state
