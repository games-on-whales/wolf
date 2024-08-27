#pragma once

#include <eventbus/event_bus.hpp>
#include <gst/gst.h>
#include <helpers/logger.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>

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

static bool run_pipeline(const std::string &pipeline_desc,
                         const std::function<immer::array<immer::box<dp::handler_registration<events::EventTypes>>>(
                             gst_element_ptr /* pipeline */, gst_main_loop_ptr /* main_loop */)> &on_pipeline_ready) {
  GError *error = nullptr;
  gst_element_ptr pipeline(gst_parse_launch(pipeline_desc.c_str(), &error), [](const auto &pipeline) {
    logs::log(logs::trace, "~pipeline");
    gst_object_unref(pipeline);
  });

  if (!pipeline) {
    logs::log(logs::error, "[GSTREAMER] Pipeline parse error: {}", error->message);
    g_error_free(error);
    return false;
  } else if (error) { // Please note that you might get a return value that is not NULL even though the error is set. In
                      // this case there was a recoverable parsing error and you can try to play the pipeline.
    logs::log(logs::warning, "[GSTREAMER] Pipeline parse error (recovered): {}", error->message);
    g_error_free(error);
  }

  gst_main_context_ptr context = {g_main_context_new(), ::g_main_context_unref};
  g_main_context_push_thread_default(context.get());
  gst_main_loop_ptr loop(g_main_loop_new(context.get(), FALSE), ::g_main_loop_unref);

  /*
   * adds a watch for new message on our pipeline's message bus to
   * the default GLib main context, which is the main context that our
   * GLib main loop is attached to below
   */
  auto bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline.get()));
  gst_bus_add_signal_watch(bus);
  g_signal_connect(bus, "message::error", G_CALLBACK(pipeline_error_handler), loop.get());
  g_signal_connect(bus, "message::eos", G_CALLBACK(pipeline_eos_handler), loop.get());
  gst_object_unref(bus);

  /* Set the pipeline to "playing" state*/
  gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(reinterpret_cast<GstBin *>(pipeline.get()),
                                    GST_DEBUG_GRAPH_SHOW_ALL,
                                    "pipeline-start");

  /* Let the calling thread set extra things */
  auto handlers = on_pipeline_ready(pipeline, loop);

  /* The main loop will be run until someone calls g_main_loop_quit() */
  g_main_loop_run(loop.get());

  /* Out of the main loop, clean up nicely */
  gst_element_set_state(pipeline.get(), GST_STATE_PAUSED);
  gst_element_set_state(pipeline.get(), GST_STATE_READY);
  gst_element_set_state(pipeline.get(), GST_STATE_NULL);

  return true;
}

/**
 * Sends a custom message in the pipeline
 */
static void send_message(GstElement *recipient, GstStructure *message) {
  auto gst_ev = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, message);
  gst_element_send_event(recipient, gst_ev);
}
} // namespace wolf::core::gstreamer