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
} // namespace wolf::core::gstreamer
