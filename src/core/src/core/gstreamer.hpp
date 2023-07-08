#pragma once

#include <core/api.hpp>
#include <eventbus/event_bus.hpp>
#include <gst/gst.h>
#include <helpers/logger.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>

namespace wolf::core::gstreamer {

using namespace wolf::core::api;

using gst_element_ptr = std::shared_ptr<GstElement>;
using gst_main_loop_ptr = std::shared_ptr<GMainLoop>;

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

static bool
run_pipeline(const std::string &pipeline_desc,
             std::size_t session_id,
             const std::shared_ptr<dp::event_bus> &event_bus,
             const std::function<immer::array<immer::box<dp::handler_registration>>(GstElement *)> &on_pipeline_ready) {
  GError *error = nullptr;
  gst_element_ptr pipeline(gst_parse_launch(pipeline_desc.c_str(), &error), [session_id](const auto &pipeline) {
    logs::log(logs::trace, "~pipeline {}", session_id);
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

  /*
   * create a mainloop that runs/iterates the default GLib main context
   * (context NULL), in other words: makes the context check if anything
   * it watches for has happened. When a message has been posted on the
   * bus, the default main context will automatically call our
   * my_bus_callback() function to notify us of that message.
   */
  gst_main_loop_ptr loop(g_main_loop_new(nullptr, FALSE), ::g_main_loop_unref);

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
  auto handlers = on_pipeline_ready(pipeline.get());

  auto pause_handler = event_bus->register_handler<immer::box<PauseStreamEvent>>(
      [session_id, loop](const immer::box<PauseStreamEvent> &ev) {
        if (ev->session_id == session_id) {
          logs::log(logs::debug, "[GSTREAMER] Pausing pipeline: {}", session_id);

          /**
           * Unfortunately here we can't just pause the pipeline,
           * when a pipeline will be resumed there are a lot of breaking changes like:
           *  - Client IP:PORT
           *  - AES key and IV for encrypted payloads
           *  - Client resolution, framerate, and encoding
           *
           *  The only solution is to kill the pipeline and re-create it again when a resume happens
           */

          g_main_loop_quit(loop.get());
        }
      });

  auto stop_handler = event_bus->register_handler<immer::box<StopStreamEvent>>(
      [session_id, loop](const immer::box<StopStreamEvent> &ev) {
        if (ev->session_id == session_id) {
          logs::log(logs::debug, "[GSTREAMER] Stopping pipeline: {}", session_id);
          g_main_loop_quit(loop.get());
        }
      });

  /* The main loop will be run until someone calls g_main_loop_quit() */
  g_main_loop_run(loop.get());

  logs::log(logs::debug, "[GSTREAMER] Ending pipeline: {}", session_id);

  /* Out of the main loop, clean up nicely */
  gst_element_set_state(pipeline.get(), GST_STATE_PAUSED);
  gst_element_set_state(pipeline.get(), GST_STATE_READY);
  gst_element_set_state(pipeline.get(), GST_STATE_NULL);

  for (const auto &handler : handlers) {
    handler->unregister();
  }
  pause_handler.unregister();
  stop_handler.unregister();

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