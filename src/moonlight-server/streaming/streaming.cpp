#include <boost/asio.hpp>
#include <core/api.hpp>
#include <core/gstreamer.hpp>
#include <functional>
#include <gst-plugin/gstrtpmoonlightpay_video.hpp>
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
  guint source_id{};
  int framerate;
  GstClockTime timestamp = 0;
};

namespace custom_src {

std::shared_ptr<GstAppDataState> setup_app_src(const immer::box<state::VideoSession> &video_session,
                                               wolf::core::virtual_display::wl_state_ptr wl_ptr) {
  return std::shared_ptr<GstAppDataState>(new GstAppDataState{.wayland_state = std::move(wl_ptr),
                                                              .source_id = 0,
                                                              .framerate = video_session->display_mode.refreshRate},
                                          [](const auto &app_data_state) {
                                            logs::log(logs::trace, "~GstAppDataState");
                                            if (app_data_state->source_id != 0) {
                                              g_source_remove(app_data_state->source_id);
                                            }
                                            delete app_data_state;
                                          });
}

static bool push_data(GstAppDataState *data) {
  GstFlowReturn ret;

  auto buffer = get_frame(*data->wayland_state);
  if (GST_IS_BUFFER(buffer) && GST_IS_APP_SRC(data->app_src.get())) {

    GST_BUFFER_PTS(buffer) = data->timestamp;
    GST_BUFFER_DTS(buffer) = data->timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, data->framerate);
    data->timestamp += GST_BUFFER_DURATION(buffer);

    // gst_app_src_push_buffer takes ownership of the buffer
    ret = gst_app_src_push_buffer(GST_APP_SRC(data->app_src.get()), buffer);
    if (ret == GST_FLOW_OK) {
      return true;
    }
  }

  logs::log(logs::debug, "[WAYLAND] Error during app-src push data");
  return false;
}

static void app_src_need_data(GstElement *pipeline, guint size, GstAppDataState *data) {
  if (data->source_id == 0) {
    logs::log(logs::debug, "[WAYLAND] Start feeding app-src");
    data->source_id = g_idle_add((GSourceFunc)push_data, data);
  }
}

static void app_src_enough_data(GstElement *pipeline, guint size, GstAppDataState *data) {
  if (data->source_id != 0) {
    logs::log(logs::debug, "[WAYLAND] Stop feeding app-src");
    g_source_remove(data->source_id);
    data->source_id = 0;
  }
}
} // namespace custom_src

namespace custom_sink {

namespace ba = boost::asio;
using namespace ba::ip;

struct AppSinkState {
  gst_rtp_moonlight_pay_video *rtpmoonlightpay;
  std::unique_ptr<udp::socket> socket;
};

std::shared_ptr<AppSinkState> setup_app_sink(const immer::box<state::VideoSession> &video_session,
                                             unsigned short client_port) {
  auto rtpmoonlightpay = (gst_rtp_moonlight_pay_video *)g_object_new(gst_TYPE_rtp_moonlight_pay_video, nullptr);
  rtpmoonlightpay->payload_size = video_session->packet_size;
  rtpmoonlightpay->fec_percentage = video_session->fec_percentage;
  rtpmoonlightpay->min_required_fec_packets = video_session->min_required_fec_packets;

  ba::io_context io_context;
  auto socket_ptr = std::make_unique<udp::socket>(io_context);
  auto endpoint = udp::endpoint(address::from_string(video_session->client_ip), client_port);
  socket_ptr->connect(endpoint);

  return std::shared_ptr<AppSinkState>(
      new AppSinkState{.rtpmoonlightpay = rtpmoonlightpay, .socket = std::move(socket_ptr)},
      [](const auto &state) {
        logs::log(logs::trace, "~AppSinkState");
        gst_object_unref(state->rtpmoonlightpay);
        state->socket->shutdown(udp::socket::shutdown_send);
      });
}

static GstFlowReturn sink_got_data(GstElement *sink, AppSinkState *state) {
  GstSample *sample;
  GstMapInfo data_info;

  g_signal_emit_by_name(sink, "pull-sample", &sample); // retrieve the buffer
  if (sample) {
    auto packets = gst_moonlight_video::split_into_rtp(state->rtpmoonlightpay, gst_sample_get_buffer(sample));
    auto nr_packets = gst_buffer_list_length(packets);
    for (int i = 0; i < nr_packets; i++) { // TODO: use boost BufferSequence instead?
      auto packet = gst_buffer_list_get(packets, i);
      gst_buffer_map(packet, &data_info, GST_MAP_READ);

      boost::system::error_code ec;
      state->socket->send(ba::buffer(data_info.data, data_info.size), 0, ec);
      if (ec) { // TODO: should we return GST_FLOW_ERROR?
        logs::log(logs::debug, "[GStreamer] Error while sending buffer: {}", ec.message());
      }

      gst_buffer_unmap(packet, &data_info);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  return GST_FLOW_ERROR;
}
} // namespace custom_sink

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
                              fmt::arg("color_range", color_range),
                              fmt::arg("host_port", video_session->port));
  logs::log(logs::debug, "Starting video pipeline: {}", pipeline);

  auto appsrc_state = custom_src::setup_app_src(video_session, std::move(wl_ptr));
  auto appsink_state = custom_sink::setup_app_sink(video_session, client_port);

  wolf::core::gstreamer::run_pipeline(
      pipeline,
      video_session->session_id,
      event_bus,
      [video_session, event_bus, appsrc_state, appsink_state](auto pipeline) {
        if (auto app_src_el = gst_bin_get_by_name(GST_BIN(pipeline), "wolf_wayland_source")) {
          logs::log(logs::debug, "Setting up wolf_wayland_source");
          g_assert(GST_IS_APP_SRC(app_src_el));

          auto app_src_ptr = wolf::core::gstreamer::gst_element_ptr(app_src_el, ::gst_object_unref);

          auto caps = set_resolution(*appsrc_state->wayland_state, video_session->display_mode, app_src_ptr);
          g_object_set(app_src_ptr.get(), "caps", caps.get(), NULL);

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
        auto idr_handler = event_bus->register_handler<immer::box<wolf::core::api::ControlEvent>>(
            [sess_id = video_session->session_id, pipeline](const immer::box<wolf::core::api::ControlEvent> &ctrl_ev) {
              if (ctrl_ev->session_id == sess_id) {
                if (ctrl_ev->type == wolf::core::api::IDR_FRAME) {
                  logs::log(logs::debug, "[GSTREAMER] Forcing IDR");
                  // Force IDR event, see: https://github.com/centricular/gstwebrtc-demos/issues/186
                  // https://gstreamer.freedesktop.org/documentation/additional/design/keyframe-force.html?gi-language=c
                  wolf::core::gstreamer::send_message(
                      pipeline,
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
                              fmt::arg("client_ip", audio_session->client_ip),
                              fmt::arg("host_port", audio_session->port));
  logs::log(logs::debug, "Starting audio pipeline: {}", pipeline);

  wolf::core::gstreamer::run_pipeline(pipeline, audio_session->session_id, event_bus, [audio_session](auto pipeline) {
    return immer::array<immer::box<dp::handler_registration>>{};
  });
}

} // namespace streaming