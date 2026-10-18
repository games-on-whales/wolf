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
 * Given a Gstreamer element returns the supported DRM formats (if any).
 * Ex: "vah265enc" -> ["P010:0x0200000000042305", "NV12:0x0200000000042305"]
 */
static std::vector<std::string> get_dma_caps(const std::string &gst_plugin_name) {
  std::vector<std::string> caps;
  GstRegistry *registry = gst_registry_get();
  if (auto feature = gst_registry_find_feature(registry, gst_plugin_name.c_str(), GST_TYPE_ELEMENT_FACTORY)) {
    if (auto real_feature = gst_registry_lookup_feature(gst_registry_get(), GST_OBJECT_NAME(feature))) {
      auto pads = gst_element_factory_get_static_pad_templates(GST_ELEMENT_FACTORY(real_feature));
      for (auto pad = pads; pad; pad = g_list_next(pad)) {
        auto pad_template = (GstStaticPadTemplate *)(pad->data);
        if (pad_template->static_caps.string && pad_template->direction == GST_PAD_SINK) {
          GstCaps *current_caps = gst_static_caps_get(&pad_template->static_caps);
          // iterate over caps looking for the type memory:DMABuf
          gst_caps_foreach(
              current_caps,
              [](GstCapsFeatures *features, GstStructure *structure, gpointer user_data) -> gboolean {
                auto caps = (std::vector<std::string> *)user_data;
                if (features && gst_caps_features_contains(features, "memory:DMABuf")) {
                  // get the array of supported formats under "drm-format" (if present)
                  GValueArray *formats = nullptr;
                  gst_structure_get_list(structure, "drm-format", &formats);
                  if (formats) {
                    for (guint i = 0; i < formats->n_values; i++) {
                      GValue *format = &formats->values[i];
                      if (G_VALUE_HOLDS_STRING(format)) {
                        caps->push_back(g_value_get_string(format));
                      }
                    }
                  }
                }
                return true;
              },
              &caps);
          gst_caps_unref(current_caps);
        }
      }
      gst_object_unref(real_feature);
    }
    gst_object_unref(feature);
  }

  return caps;
}
} // namespace wolf::core::gstreamer