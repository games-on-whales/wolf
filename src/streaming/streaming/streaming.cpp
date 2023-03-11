#include <functional>
#include <gstreamer-1.0/gst/app/gstappsrc.h>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/box.hpp>
#include <input/input.hpp>
#include <memory>
#include <streaming/data-structures.hpp>
#include <streaming/gst-plugin/gstrtpmoonlightpay_audio.hpp>
#include <streaming/gst-plugin/gstrtpmoonlightpay_video.hpp>
#include <streaming/streaming.hpp>
#include <streaming/wayland-display.hpp>

namespace streaming {

/**
 * GStreamer needs to be initialised once per run
 * Call this method in your main.
 */
void init() {
  /* It is also possible to call the init function with two NULL arguments,
   * in which case no command line options will be parsed by GStreamer.
   */
  gst_init(nullptr, nullptr);
  logs::log(logs::info, "Gstreamer version: {}", get_gst_version());

  GstPlugin *video_plugin = gst_plugin_load_by_name("rtpmoonlightpay_video");
  gst_element_register(video_plugin, "rtpmoonlightpay_video", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_video);

  GstPlugin *audio_plugin = gst_plugin_load_by_name("rtpmoonlightpay_audio");
  gst_element_register(audio_plugin, "rtpmoonlightpay_audio", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_audio);

  moonlight::fec::init();
}

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

bool run_pipeline(
    const std::string &pipeline_desc,
    std::size_t session_id,
    const std::shared_ptr<dp::event_bus> &event_bus,
    const std::function<immer::array<immer::box<dp::handler_registration>>(gst_element_ptr, gst_main_loop_ptr)>
        &on_pipeline_ready) {
  GError *error = nullptr;
  gst_element_ptr pipeline(gst_parse_launch(pipeline_desc.c_str(), &error), ::gst_object_unref);

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
  auto handlers = on_pipeline_ready(pipeline, loop);

  auto pause_handler = event_bus->register_handler<immer::box<moonlight::PauseStreamEvent>>(
      [session_id, loop](const immer::box<moonlight::PauseStreamEvent> &ev) {
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

  auto stop_handler = event_bus->register_handler<immer::box<moonlight::StopStreamEvent>>(
      [session_id, loop](const immer::box<moonlight::StopStreamEvent> &ev) {
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
void send_message(GstElement *recipient, GstStructure *message) {
  auto gst_ev = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, message);
  gst_element_send_event(recipient, gst_ev);
}

struct GstAppDataState {
  gst_element_ptr app_src;
  std::shared_ptr<WaylandState> wayland_state;
  guint source_id{};
};

bool push_data(GstAppDataState *data) {
  GstFlowReturn ret;

  auto buffer = display_get_frame(*data->wayland_state->display);
  if (GST_IS_BUFFER(buffer) && GST_IS_APP_SRC(data->app_src.get())) {
    g_signal_emit_by_name(data->app_src.get(), "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    if (ret == GST_FLOW_OK) {
      return true;
    }
  }

  logs::log(logs::debug, "[WAYLAND] Error during app-src push data");
  return false;
}

static void app_src_need_data(GstElement *pipeline, guint size, GstAppDataState *data) {
  if (data->source_id == 0) {
    logs::log(logs::trace, "[WAYLAND] Start feeding app-src");
    data->source_id = g_idle_add((GSourceFunc)push_data, data);
  }
}

static void app_src_enough_data(GstElement *pipeline, guint size, GstAppDataState *data) {
  if (data->source_id != 0) {
    logs::log(logs::trace, "[WAYLAND] Stop feeding app-src");
    g_source_remove(data->source_id);
    data->source_id = 0;
  }
}

/**
 * Start VIDEO pipeline
 */
void start_streaming_video(const immer::box<state::VideoSession> &video_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           const std::shared_ptr<WaylandState> &wl_ptr,
                           unsigned short client_port) {
  std::string color_range = (static_cast<int>(video_session->color_range) == static_cast<int>(state::JPEG)) ? "jpeg"
                                                                                                            : "mpeg2";
  std::string color_space;
  switch (static_cast<int>(video_session->color_space)) {
  case state::BT601:
    color_space = "bt601";
    break;
  case state::BT709:
    color_space = "bt709";
    break;
  case state::BT2020:
    color_space = "bt2020";
    break;
  }

  auto pipeline = fmt::format(video_session->gst_pipeline,
                              fmt::arg("width", video_session->display_mode.width),
                              fmt::arg("height", video_session->display_mode.height),
                              fmt::arg("fps", video_session->display_mode.refreshRate),
                              fmt::arg("bitrate", video_session->bitrate_kbps),
                              fmt::arg("client_port", client_port),
                              fmt::arg("client_ip", video_session->client_ip),
                              fmt::arg("payload_size", video_session->packet_size),
                              fmt::arg("fec_percentage", video_session->fec_percentage),
                              fmt::arg("min_required_fec_packets", video_session->min_required_fec_packets),
                              fmt::arg("slices_per_frame", video_session->slices_per_frame),
                              fmt::arg("color_space", color_space),
                              fmt::arg("color_range", color_range));
  logs::log(logs::debug, "Starting video pipeline: {}", pipeline);
  auto app_src_state = std::shared_ptr<GstAppDataState>(new GstAppDataState{.wayland_state = wl_ptr, .source_id = 0},
                                                        [](const auto &app_data_state) {
                                                          logs::log(logs::trace, "free(app_data_state)");
                                                          g_source_remove(app_data_state->source_id);
                                                          delete app_data_state;
                                                        });

  run_pipeline(pipeline,
               video_session->session_id,
               event_bus,
               [video_session, event_bus, wl_ptr, app_src_state](auto pipeline, auto loop) {
                 if (auto app_src_el = gst_bin_get_by_name(GST_BIN(pipeline.get()), "wolf_wayland_source")) {
                   g_assert(GST_IS_APP_SRC(app_src_el));

                   auto app_src_ptr = gst_element_ptr(app_src_el, [](const auto &app_src) {
                     logs::log(logs::trace, "gst_object_unref(app_src)");
                     gst_object_unref(app_src);
                   });
                   set_resolution(wl_ptr, video_session->display_mode, app_src_ptr);
                   app_src_state->app_src = std::move(app_src_ptr);

                   /* Adapted from the tutorial at:
                    * https://gstreamer.freedesktop.org/documentation/tutorials/basic/short-cutting-the-pipeline.html?gi-language=c*/
                   g_signal_connect(app_src_el, "need-data", G_CALLBACK(app_src_need_data), app_src_state.get());
                   g_signal_connect(app_src_el, "enough-data", G_CALLBACK(app_src_enough_data), app_src_state.get());
                 }

                 /*
                  * The force IDR event will be triggered by the control stream.
                  * We have to pass this back into the gstreamer pipeline
                  * in order to force the encoder to produce a new IDR packet
                  */
                 auto idr_handler = event_bus->register_handler<immer::box<ControlEvent>>(
                     [sess_id = video_session->session_id, pipeline](const immer::box<ControlEvent> &ctrl_ev) {
                       if (ctrl_ev->session_id == sess_id) {
                         if (ctrl_ev->type == IDR_FRAME) {
                           logs::log(logs::debug, "[GSTREAMER] Forcing IDR");
                           // Force IDR event, see: https://github.com/centricular/gstwebrtc-demos/issues/186
                           send_message(
                               pipeline.get(),
                               gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, TRUE, NULL));
                         }
                       }
                     });

                 return immer::array<immer::box<dp::handler_registration>>{std::move(idr_handler)};
               });
}

/**
 * Start AUDIO pipeline
 */
void start_streaming_audio(const immer::box<state::AudioSession> &audio_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           unsigned short client_port,
                           const std::string &sink_name,
                           const std::string &server_name) {
  auto pipeline = fmt::format(audio_session->gst_pipeline,
                              fmt::arg("channels", audio_session->channels),
                              fmt::arg("bitrate", audio_session->bitrate),
                              fmt::arg("sink_name", sink_name),
                              fmt::arg("server_name", server_name),
                              fmt::arg("packet_duration", audio_session->packet_duration),
                              fmt::arg("aes_key", audio_session->aes_key),
                              fmt::arg("aes_iv", audio_session->aes_iv),
                              fmt::arg("encrypt", audio_session->encrypt_audio),
                              fmt::arg("client_port", client_port),
                              fmt::arg("client_ip", audio_session->client_ip));
  logs::log(logs::debug, "Starting audio pipeline: {}", pipeline);

  run_pipeline(pipeline, audio_session->session_id, event_bus, [audio_session](auto pipeline, auto loop) {
    return immer::array<immer::box<dp::handler_registration>>{};
  });
}

} // namespace streaming