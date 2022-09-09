#include <fmt/core.h>
#include <fmt/format.h>
#include <gst/gst.h>
#include <immer/box.hpp>
#include <moonlight/data-structures.hpp>
#include <streaming/data-structures.hpp>
#include <streaming/gst-plugin/gstrtpmoonlightpay.cpp>
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
void init(int argc, char *argv[]) {
  gst_init(&argc, &argv);
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

void start_streaming(immer::box<state::VideoSession> video_session, unsigned short client_port) {
  GstElement *pipeline;
  GError *error = nullptr;

  GstPlugin *plugin = nullptr;
  GST_ELEMENT_REGISTER(rtpmoonlightpay, plugin);

  // see an example pipeline at: https://gist.github.com/esrever10/7d39fe2d4163c5b2d7006495c3c911bb
  pipeline = gst_parse_launch(
      fmt::format(
          "videotestsrc pattern=ball is-live=true ! "
          "videoscale ! "
          "videoconvert ! "
          "videorate ! "
          "video/x-raw, width={width}, height={height}, framerate={fps}/1, format=I420 ! "
          "x264enc pass=cbr tune=zerolatency bitrate={bitrate} speed-preset=superfast byte-stream=true aud=false ! "
          "rtpmoonlightpay name=moonlight_pay payload_size={payload_size} fec_percentage={fec_percentage} "
          "min_required_fec_packets={min_required_fec_packets}"
          " ! "
          //                        "fakesink dump=true",
          "udpsink host={client_ip} port={client_port}",
          fmt::arg("width", video_session->width),
          fmt::arg("height", video_session->height),
          fmt::arg("fps", video_session->refreshRate),
          fmt::arg("bitrate", video_session->bitrate_kbps),
          fmt::arg("client_port", client_port),
          fmt::arg("client_ip", video_session->client_ip),
          fmt::arg("payload_size", video_session->packet_size),
          fmt::arg("fec_percentage", video_session->fec_percentage),
          fmt::arg("min_required_fec_packets", video_session->min_required_fec_packets))
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
            g_print("Terminating Gstreamer pipeline\n");
            stop = true;
          }
        }
      });

  /* Set the pipeline to "playing" state*/
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  while (!stop) {
    auto message = gst_bus_poll(GST_ELEMENT_BUS(pipeline), GST_MESSAGE_ERROR, 5 * GST_MSECOND);

    if (message) {
      g_print("Error while running GStreamer pipeline!! \n");
      stop = true;
      gst_message_unref(message);
    }
  }

  /* Out of the main loop, clean up nicely */
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  gst_element_set_state(pipeline, GST_STATE_READY);
  gst_element_set_state(pipeline, GST_STATE_NULL);

  ev_handler.unregister();
  gst_object_unref(moonlight_plugin);
  gst_object_unref(GST_OBJECT(pipeline));
}

} // namespace streaming