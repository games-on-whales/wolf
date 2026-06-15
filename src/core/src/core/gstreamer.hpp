#pragma once

#include <gst/gst.h>
#include <helpers/logger.hpp>

namespace wolf::core::gstreamer {

using gst_element_ptr = std::shared_ptr<GstElement>;
using gst_main_loop_ptr = std::shared_ptr<GMainLoop>;
using gst_main_context_ptr = std::shared_ptr<GMainContext>;

static void pipeline_error_handler(GstBus *bus, GstMessage *message, gpointer data) {
  auto loop = (GMainLoop *)data;
  GError *err;
  gchar *debug;
  gst_message_parse_error(message, &err, &debug);
  logs::log(logs::error, "[GSTREAMER] Pipeline error: {}", err->message);
  g_error_free(err);
  g_free(debug);

  /* Terminate pipeline on error */
  g_main_loop_quit(loop);
}

static void pipeline_eos_handler(GstBus *bus, GstMessage *message, gpointer data) {
  auto loop = (GMainLoop *)data;
  logs::log(logs::info, "[GSTREAMER] Pipeline reached End Of Stream");
  g_main_loop_quit(loop);
}

/**
 * Sends a custom message in the pipeline
 */
static void send_message(GstElement *recipient, GstStructure *message) {
  if (!recipient) {
    // Avoid throwing errors when we don't have a recipient
    // This might happen when we have the control stream connected, but the wayland display isn't ready yet
    return;
  }
  auto gst_ev = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, message);
  gst_element_send_event(recipient, gst_ev);
}

/**
 * Collect the DRM formats listed under "drm-format" in any memory:DMABuf
 * structure of the given caps. Handles both the "drm-format" as a list (how
 * VA elements like vapostproc advertise their static caps) and as a single
 * string per structure (how waylanddisplaysrc advertises its runtime caps).
 */
static std::vector<std::string> parse_dma_drm_formats(GstCaps *caps_to_scan) {
  std::vector<std::string> caps;
  if (!caps_to_scan) {
    return caps;
  }
  gst_caps_foreach(
      caps_to_scan,
      [](GstCapsFeatures *features, GstStructure *structure, gpointer user_data) -> gboolean {
        auto caps = (std::vector<std::string> *)user_data;
        if (features && gst_caps_features_contains(features, "memory:DMABuf")) {
          // "drm-format" as a list of formats ...
          GValueArray *formats = nullptr;
          gst_structure_get_list(structure, "drm-format", &formats);
          if (formats) {
            for (guint i = 0; i < formats->n_values; i++) {
              GValue *format = &formats->values[i];
              if (G_VALUE_HOLDS_STRING(format)) {
                caps->push_back(g_value_get_string(format));
              }
            }
          } else if (const char *single = gst_structure_get_string(structure, "drm-format")) {
            // ... or a single format string for this structure
            caps->push_back(single);
          }
        }
        return true;
      },
      &caps);
  return caps;
}

/**
 * Given a Gstreamer element returns the supported DRM formats (if any) for the
 * requested pad direction, as read from its *static* pad templates.
 * Ex: "vapostproc" -> ["P010:0x0200000000042305", "NV12:0x0200000000042305"]
 */
static std::vector<std::string> get_dma_caps(const std::string &gst_plugin_name, GstPadDirection direction) {
  std::vector<std::string> caps;
  GstRegistry *registry = gst_registry_get();
  if (auto feature = gst_registry_find_feature(registry, gst_plugin_name.c_str(), GST_TYPE_ELEMENT_FACTORY)) {
    if (auto real_feature = gst_registry_lookup_feature(gst_registry_get(), GST_OBJECT_NAME(feature))) {
      auto pads = gst_element_factory_get_static_pad_templates(GST_ELEMENT_FACTORY(real_feature));
      for (auto pad = pads; pad; pad = g_list_next(pad)) {
        auto pad_template = (GstStaticPadTemplate *)(pad->data);
        if (pad_template->static_caps.string && pad_template->direction == direction) {
          GstCaps *current_caps = gst_static_caps_get(&pad_template->static_caps);
          auto pad_caps = parse_dma_drm_formats(current_caps);
          caps.insert(caps.end(), pad_caps.begin(), pad_caps.end());
          gst_caps_unref(current_caps);
        }
      }
      gst_object_unref(real_feature);
    }
    gst_object_unref(feature);
  }

  return caps;
}

static std::vector<std::string> get_dma_caps(const std::string &gst_plugin_name) {
  return get_dma_caps(gst_plugin_name, GST_PAD_SINK);
}

/**
 * Like get_dma_caps but queries a *live* instance of the element so we can read
 * caps that are only known at runtime. waylanddisplaysrc, for instance, only
 * advertises the render-node's actual drm-formats once it has opened the device
 * (its static pad template just says DMA_DRM with no drm-format list).
 *
 * Instantiates the element, applies the given properties, brings it to PAUSED
 * (which opens the device and lets the element compute its real caps), queries
 * the requested pad and tears it back down. Returns an empty list on failure so
 * callers can fall back to the legacy pipeline.
 */
static std::vector<std::string>
get_dma_caps_runtime(const std::string &gst_plugin_name,
                     const std::vector<std::pair<std::string, std::string>> &properties = {},
                     const std::string &pad_name = "src") {
  std::vector<std::string> caps;
  GstElement *element = gst_element_factory_make(gst_plugin_name.c_str(), nullptr);
  if (!element) {
    logs::log(logs::warning, "[GSTREAMER] Unable to create '{}' to query runtime DMA caps", gst_plugin_name);
    return caps;
  }
  for (const auto &[key, value] : properties) {
    gst_util_set_object_arg(G_OBJECT(element), key.c_str(), value.c_str());
  }

  // PAUSED makes the element open the render node and compute its real caps.
  // waylanddisplaysrc is a live source, so PAUSED returns NO_PREROLL rather than
  // SUCCESS; only an outright FAILURE means we couldn't open the device.
  auto ret = gst_element_set_state(element, GST_STATE_PAUSED);
  if (ret != GST_STATE_CHANGE_FAILURE) {
    ret = gst_element_get_state(element, nullptr, nullptr, 5 * GST_SECOND);
  }
  if (ret == GST_STATE_CHANGE_FAILURE) {
    logs::log(logs::warning,
              "[GSTREAMER] '{}' failed to reach PAUSED while querying runtime DMA caps",
              gst_plugin_name);
  } else if (auto pad = gst_element_get_static_pad(element, pad_name.c_str())) {
    if (auto pad_caps = gst_pad_query_caps(pad, nullptr)) {
      caps = parse_dma_drm_formats(pad_caps);
      gst_caps_unref(pad_caps);
    }
    gst_object_unref(pad);
  }

  gst_element_set_state(element, GST_STATE_NULL);
  gst_object_unref(element);
  return caps;
}
} // namespace wolf::core::gstreamer
