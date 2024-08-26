#include <control/control.hpp>
#include <core/gstreamer.hpp>
#include <functional>
#include <gst-plugin/video.hpp>
#include <gstreamer-1.0/gst/app/gstappsink.h>
#include <gstreamer-1.0/gst/app/gstappsrc.h>
#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/box.hpp>
#include <memory>
#include <streaming/data-structures.hpp>
#include <streaming/streaming.hpp>

namespace streaming {

struct GstAppDataState {
  wolf::core::gstreamer::gst_element_ptr app_src;
  wolf::core::virtual_display::wl_state_ptr wayland_state;
  GMainContext *context;
  GSource *source;
  int framerate;
  GstClockTime timestamp = 0;
};

namespace custom_src {

std::shared_ptr<GstAppDataState> setup_app_src(const immer::box<state::VideoSession> &video_session,
                                               wolf::core::virtual_display::wl_state_ptr wl_ptr) {
  return std::shared_ptr<GstAppDataState>(new GstAppDataState{.wayland_state = std::move(wl_ptr),
                                                              .source = nullptr,
                                                              .framerate = video_session->display_mode.refreshRate},
                                          [](const auto &app_data_state) {
                                            logs::log(logs::trace, "~GstAppDataState");
                                            if (app_data_state->source) {
                                              g_source_destroy(app_data_state->source);
                                              app_data_state->source = nullptr;
                                            }
                                            delete app_data_state;
                                          });
}

static bool push_data(GstAppDataState *data) {
  GstFlowReturn ret;

  if (data->wayland_state) {
    auto buffer = get_frame(*data->wayland_state);
    /**
     * get_frame() will internally sleep until vsync or a new frame is available.
     * we have to make sure that the pipeline is still running or we might access some invalid pointer
     **/
    if (GST_IS_BUFFER(buffer) && data->source && GST_IS_APP_SRC(data->app_src.get())) {

      GST_BUFFER_PTS(buffer) = data->timestamp;
      GST_BUFFER_DTS(buffer) = data->timestamp;
      GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, data->framerate);
      data->timestamp += GST_BUFFER_DURATION(buffer);

      // gst_app_src_push_buffer takes ownership of the buffer
      ret = gst_app_src_push_buffer(GST_APP_SRC(data->app_src.get()), buffer);
      if (ret == GST_FLOW_OK) {
        return true;
      }
    } else {
      gst_buffer_unref(buffer);
    }
  }

  logs::log(logs::debug, "[WAYLAND] Error during app-src push data");
  return false;
}

static void app_src_need_data(GstElement *pipeline, guint size, GstAppDataState *data) {
  if (!data->source) {
    logs::log(logs::debug, "[WAYLAND] Start feeding app-src");
    data->source = g_idle_source_new();
    g_source_attach(data->source, data->context);
    g_source_set_callback(data->source, (GSourceFunc)push_data, data, NULL);
  }
}

static void app_src_enough_data(GstElement *pipeline, guint size, GstAppDataState *data) {
  if (data->source) {
    logs::log(logs::trace, "app_src_enough_data");
    g_source_destroy(data->source);
    data->source = nullptr;
  }
}
} // namespace custom_src

using namespace wolf::core::gstreamer;

/**
 * Start VIDEO pipeline
 */
void start_streaming_video(const immer::box<state::VideoSession> &video_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           wolf::core::virtual_display::wl_state_ptr wl_ptr,
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

  auto pipeline = fmt::format(fmt::runtime(video_session->gst_pipeline),
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
                              fmt::arg("color_range", color_range),
                              fmt::arg("host_port", video_session->port));
  logs::log(logs::debug, "Starting video pipeline: \n{}", pipeline);

  auto appsrc_state = custom_src::setup_app_src(video_session, std::move(wl_ptr));

  run_pipeline(pipeline, [video_session, event_bus, appsrc_state](auto pipeline, auto loop) {
    if (auto app_src_el = gst_bin_get_by_name(GST_BIN(pipeline.get()), "wolf_wayland_source")) {
      appsrc_state->context = g_main_context_get_thread_default();
      logs::log(logs::debug, "Setting up wolf_wayland_source");
      g_assert(GST_IS_APP_SRC(app_src_el));

      auto app_src_ptr = wolf::core::gstreamer::gst_element_ptr(app_src_el, ::gst_object_unref);

      auto caps = set_resolution(*appsrc_state->wayland_state, video_session->display_mode, app_src_ptr);
      g_object_set(app_src_ptr.get(), "caps", caps.get(), NULL);
      // No seeking is supported, this is a live stream
      g_object_set(app_src_el, "stream-type", GST_APP_STREAM_TYPE_STREAM, NULL);
      // appsrc will drop any buffers that are pushed into it once its internal queue is full
      g_object_set(app_src_el, "leaky-type", GST_APP_LEAKY_TYPE_DOWNSTREAM, NULL);
      // sometimes the encoder or the network sink might lag behind, we'll keep up to 3 buffers in the queue
      g_object_set(app_src_el, "max-buffers", 3, NULL);

      /* Adapted from the tutorial at:
       * https://gstreamer.freedesktop.org/documentation/tutorials/basic/short-cutting-the-pipeline.html?gi-language=c*/
      g_signal_connect(app_src_el, "need-data", G_CALLBACK(custom_src::app_src_need_data), appsrc_state.get());
      g_signal_connect(app_src_el, "enough-data", G_CALLBACK(custom_src::app_src_enough_data), appsrc_state.get());
      appsrc_state->app_src = std::move(app_src_ptr);
    }

    /*
     * The force IDR event will be triggered by the control stream.
     * We have to pass this back into the gstreamer pipeline
     * in order to force the encoder to produce a new IDR packet
     */
    auto idr_handler = event_bus->register_handler<immer::box<control::ControlEvent>>(
        [sess_id = video_session->session_id, pipeline](const immer::box<control::ControlEvent> &ctrl_ev) {
          if (ctrl_ev->session_id == sess_id) {
            if (ctrl_ev->type == moonlight::control::pkts::IDR_FRAME) {
              logs::log(logs::debug, "[GSTREAMER] Forcing IDR");
              // Force IDR event, see: https://github.com/centricular/gstwebrtc-demos/issues/186
              // https://gstreamer.freedesktop.org/documentation/additional/design/keyframe-force.html?gi-language=c
              wolf::core::gstreamer::send_message(
                  pipeline.get(),
                  gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, TRUE, NULL));
            }
          }
        });

    auto pause_handler = event_bus->register_handler<immer::box<control::PauseStreamEvent>>(
        [sess_id = video_session->session_id, loop](const immer::box<control::PauseStreamEvent> &ev) {
          if (ev->session_id == sess_id) {
            logs::log(logs::debug, "[GSTREAMER] Pausing pipeline: {}", sess_id);

            /**
             * Unfortunately here we can't just pause the pipeline,
             * when a pipeline will be resumed there are a lot of breaking changes
             * like:
             *  - Client IP:PORT
             *  - AES key and IV for encrypted payloads
             *  - Client resolution, framerate, and encoding
             *
             *  The only solution is to kill the pipeline and re-create it again
             * when a resume happens
             */

            g_main_loop_quit(loop.get());
          }
        });

    auto stop_handler = event_bus->register_handler<immer::box<control::StopStreamEvent>>(
        [sess_id = video_session->session_id, loop](const immer::box<control::StopStreamEvent> &ev) {
          if (ev->session_id == sess_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping pipeline: {}", sess_id);
            g_main_loop_quit(loop.get());
          }
        });

    return immer::array<immer::box<dp::handler_registration>>{std::move(idr_handler),
                                                              std::move(pause_handler),
                                                              std::move(stop_handler)};
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
  auto pipeline = fmt::format(
      fmt::runtime(audio_session->gst_pipeline),
      fmt::arg("channels", audio_session->audio_mode.channels),
      fmt::arg("bitrate", audio_session->audio_mode.bitrate),
      // TODO: opusenc hardcodes those two
      // https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/1.24.6/subprojects/gst-plugins-base/ext/opus/gstopusenc.c#L661-666
      fmt::arg("streams", audio_session->audio_mode.streams),
      fmt::arg("coupled_streams", audio_session->audio_mode.coupled_streams),
      fmt::arg("sink_name", sink_name),
      fmt::arg("server_name", server_name),
      fmt::arg("packet_duration", audio_session->packet_duration),
      fmt::arg("aes_key", audio_session->aes_key),
      fmt::arg("aes_iv", audio_session->aes_iv),
      fmt::arg("encrypt", audio_session->encrypt_audio),
      fmt::arg("client_port", client_port),
      fmt::arg("client_ip", audio_session->client_ip),
      fmt::arg("host_port", audio_session->port));
  logs::log(logs::debug, "Starting audio pipeline: \n{}", pipeline);

  run_pipeline(pipeline, [session_id = audio_session->session_id, event_bus](auto pipeline, auto loop) {
    auto pause_handler = event_bus->register_handler<immer::box<control::PauseStreamEvent>>(
        [session_id, loop](const immer::box<control::PauseStreamEvent> &ev) {
          if (ev->session_id == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Pausing pipeline: {}", session_id);

            /**
             * Unfortunately here we can't just pause the pipeline,
             * when a pipeline will be resumed there are a lot of breaking changes
             * like:
             *  - Client IP:PORT
             *  - AES key and IV for encrypted payloads
             *  - Client resolution, framerate, and encoding
             *
             *  The only solution is to kill the pipeline and re-create it again
             * when a resume happens
             */

            g_main_loop_quit(loop.get());
          }
        });

    auto stop_handler = event_bus->register_handler<immer::box<control::StopStreamEvent>>(
        [session_id, loop](const immer::box<control::StopStreamEvent> &ev) {
          if (ev->session_id == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping pipeline: {}", session_id);
            g_main_loop_quit(loop.get());
          }
        });

    return immer::array<immer::box<dp::handler_registration>>{std::move(pause_handler), std::move(stop_handler)};
  });
}

} // namespace streaming