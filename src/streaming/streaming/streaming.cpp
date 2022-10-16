extern "C" {
#include <moonlight-common-c/reedsolomon/rs.h>
}

#include <fmt/core.h>
#include <fmt/format.h>
#include <gst/gst.h>
#include <immer/box.hpp>
#include <moonlight/data-structures.hpp>
#include <streaming/data-structures.hpp>
#include <streaming/gst-plugin/gstrtpmoonlightpay_audio.hpp>
#include <streaming/gst-plugin/gstrtpmoonlightpay_video.hpp>
#include <string>

namespace streaming {

using namespace moonlight::control;

/**
 * GStreamer needs to be initialised once per run
 * Call this method in your main.
 *
 * It is also possible to call the init function with two NULL arguments,
 * in which case no command line options will be parsed by GStreamer.
 */
void init() {
  gst_init(nullptr, nullptr);

  GstPlugin *video_plugin = gst_plugin_load_by_name("rtpmoonlightpay_video");
  gst_element_register(video_plugin, "rtpmoonlightpay_video", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_video);

  GstPlugin *audio_plugin = gst_plugin_load_by_name("rtpmoonlightpay_audio");
  gst_element_register(audio_plugin, "rtpmoonlightpay_audio", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_audio);

  reed_solomon_init();
}

/**
 * @return the Gstreamer version we are linked to
 */
std::string version() {
  guint major, minor, micro, nano;
  gst_version(&major, &minor, &micro, &nano);
  return fmt::format("{}.{}.{}-{}", major, minor, micro, nano);
}

/**
 * Start VIDEO pipeline
 */
void start_streaming_video(immer::box<state::VideoSession> video_session, unsigned short client_port) {
  GstElement *pipeline;
  GError *error = nullptr;

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

  // see an example pipeline at: https://gist.github.com/esrever10/7d39fe2d4163c5b2d7006495c3c911bb
  pipeline = gst_parse_launch(fmt::format(video_session->gst_pipeline,
                                          fmt::arg("width", video_session->display_mode.width),
                                          fmt::arg("height", video_session->display_mode.height),
                                          fmt::arg("fps", video_session->display_mode.refreshRate),
                                          fmt::arg("bitrate", video_session->bitrate_kbps),
                                          fmt::arg("client_port", client_port),
                                          fmt::arg("client_ip", video_session->client_ip),
                                          fmt::arg("payload_size", video_session->packet_size),
                                          fmt::arg("fec_percentage", video_session->fec_percentage),
                                          fmt::arg("min_required_fec_packets", video_session->min_required_fec_packets),
                                          fmt::arg("color_space", color_space),
                                          fmt::arg("color_range", color_range))
                                  .c_str(),
                              &error);

  if (!pipeline) {
    g_print("Parse error: %s\n", error->message);
    return;
  }

  auto moonlight_plugin = gst_bin_get_by_name(reinterpret_cast<GstBin *>(pipeline), "moonlight_pay");
  bool stop = false;

  auto ev_handler = video_session->event_bus->register_handler<immer::box<ControlEvent>>(
      [sess_id = video_session->session_id, &stop, &moonlight_plugin](immer::box<ControlEvent> ctrl_ev) {
        if (ctrl_ev->session_id == sess_id) {
          if (ctrl_ev->type == IDR_FRAME) {
            // Force IDR event, see: https://github.com/centricular/gstwebrtc-demos/issues/186
            auto gst_ev = gst_event_new_custom(
                GST_EVENT_CUSTOM_UPSTREAM,
                gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, TRUE, NULL));

            gst_element_send_event(moonlight_plugin, gst_ev);
          } else if (ctrl_ev->type == TERMINATION) {
            g_print("Terminating Gstreamer Video pipeline\n");
            stop = true;
          }
        }
      });

  /* Set the pipeline to "playing" state*/
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  //  GST_DEBUG_BIN_TO_DOT_FILE(reinterpret_cast<GstBin *>(pipeline), GST_DEBUG_GRAPH_SHOW_STATES, "pipeline-start");

  while (!stop) {
    auto message = gst_bus_poll(GST_ELEMENT_BUS(pipeline), GST_MESSAGE_ERROR, 5 * GST_MSECOND);

    if (message) {
      g_print("Error while running GStreamer pipeline!! \n");
      stop = true;
      gst_message_unref(message);
    }
  }

  //  GST_DEBUG_BIN_TO_DOT_FILE(reinterpret_cast<GstBin *>(pipeline), GST_DEBUG_GRAPH_SHOW_STATES, "pipeline-end");

  /* Out of the main loop, clean up nicely */
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  gst_element_set_state(pipeline, GST_STATE_READY);
  gst_element_set_state(pipeline, GST_STATE_NULL);

  ev_handler.unregister();
  gst_object_unref(moonlight_plugin);
  gst_object_unref(GST_OBJECT(pipeline));
}

/**
 * Start AUDIO pipeline
 */
void start_streaming_audio(immer::box<state::AudioSession> audio_session, unsigned short client_port) {
  GstElement *pipeline;
  GError *error = nullptr;

  pipeline = gst_parse_launch(fmt::format(audio_session->gst_pipeline,
                                          fmt::arg("channels", audio_session->channels),
                                          fmt::arg("bitrate", audio_session->bitrate),
                                          fmt::arg("packet_duration", audio_session->packet_duration),
                                          fmt::arg("aes_key", audio_session->aes_key),
                                          fmt::arg("aes_iv", audio_session->aes_iv),
                                          fmt::arg("encrypt", audio_session->encrypt_audio),
                                          fmt::arg("client_port", client_port),
                                          fmt::arg("client_ip", audio_session->client_ip),
                                          fmt::arg("stream_type", "audio"))
                                  .c_str(),
                              &error);

  if (!pipeline) {
    g_print("Parse error: %s\n", error->message);
    return;
  }

  bool stop = false;

  auto ev_handler = audio_session->event_bus->register_handler<immer::box<ControlEvent>>(
      [sess_id = audio_session->session_id, &stop](immer::box<ControlEvent> ctrl_ev) {
        if (ctrl_ev->session_id == sess_id) {
          if (ctrl_ev->type == TERMINATION) {
            g_print("Terminating Gstreamer Audio pipeline\n");
            stop = true;
          }
        }
      });

  /* Set the pipeline to "playing" state*/
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  //  GST_DEBUG_BIN_TO_DOT_FILE(reinterpret_cast<GstBin *>(pipeline), GST_DEBUG_GRAPH_SHOW_STATES, "pipeline-start");

  while (!stop) {
    auto message = gst_bus_poll(GST_ELEMENT_BUS(pipeline), GST_MESSAGE_ERROR, 5 * GST_MSECOND);

    if (message) {
      g_print("Error while running GStreamer pipeline!! \n");
      stop = true;
      gst_message_unref(message);
    }
  }

  //  GST_DEBUG_BIN_TO_DOT_FILE(reinterpret_cast<GstBin *>(pipeline), GST_DEBUG_GRAPH_SHOW_STATES, "pipeline-end");

  /* Out of the main loop, clean up nicely */
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  gst_element_set_state(pipeline, GST_STATE_READY);
  gst_element_set_state(pipeline, GST_STATE_NULL);

  ev_handler.unregister();
  gst_object_unref(GST_OBJECT(pipeline));
}

} // namespace streaming