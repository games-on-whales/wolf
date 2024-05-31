#pragma once
#include "gst-plugin/gsttracy.hpp"
#include "moonlight/fec.hpp"
#include <boost/asio.hpp>
#include <core/virtual-display.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <gst-plugin/gstrtpmoonlightpay_audio.hpp>
#include <gst-plugin/gstrtpmoonlightpay_video.hpp>
#include <gst/gst.h>
#include <immer/box.hpp>
#include <memory>
#include <streaming/data-structures.hpp>

namespace streaming {

void start_streaming_video(const immer::box<state::VideoSession> &video_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           wolf::core::virtual_display::wl_state_ptr wl_state,
                           unsigned short client_port);

void start_streaming_audio(const immer::box<state::AudioSession> &audio_session,
                           const std::shared_ptr<dp::event_bus> &event_bus,
                           unsigned short client_port,
                           const std::string &sink_name,
                           const std::string &server_name);

/**
 * @return the Gstreamer version we are linked to
 */
inline std::string get_gst_version() {
  guint major, minor, micro, nano;
  gst_version(&major, &minor, &micro, &nano);
  return fmt::format("{}.{}.{}-{}", major, minor, micro, nano);
}

static inline void gst_debug_fn(GstDebugCategory *category,
                                GstDebugLevel level,
                                const gchar *file,
                                const gchar *function,
                                gint line,
                                GObject *object,
                                GstDebugMessage *message,
                                gpointer user_data) {
  auto msg_str = gst_debug_message_get(message);
  auto level_name = gst_debug_level_get_name(level);
  auto category_name = gst_debug_category_get_name(category);
  logs::log(logs::parse_level(level_name),
            "[GSTREAMER] {} - {} {}:{}:{}",
            category_name,
            msg_str,
            file,
            function,
            line);
}

/**
 * GStreamer needs to be initialised once per run
 * Call this method in your main.
 */
inline void init() {
  /* It is also possible to call the init function with two NULL arguments,
   * in which case no command line options will be parsed by GStreamer.
   */
  GstPlugin *tracy_plugin = gst_plugin_load_by_name("gsttracy");
  gst_tracer_register(tracy_plugin, "gsttracy", GST_TYPE_TRACY_TRACER);

  gst_init(nullptr, nullptr);
  gst_debug_remove_log_function(gst_debug_log_default);
  gst_debug_add_log_function(gst_debug_fn, nullptr, nullptr);
  logs::log(logs::info, "Gstreamer version: {}", get_gst_version());

  GstPlugin *video_plugin = gst_plugin_load_by_name("rtpmoonlightpay_video");
  gst_element_register(video_plugin, "rtpmoonlightpay_video", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_video);

  GstPlugin *audio_plugin = gst_plugin_load_by_name("rtpmoonlightpay_audio");
  gst_element_register(audio_plugin, "rtpmoonlightpay_audio", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_audio);

  moonlight::fec::init();
}

} // namespace streaming