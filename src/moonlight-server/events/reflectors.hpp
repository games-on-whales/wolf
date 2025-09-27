#pragma once

#include <events/events.hpp>
#include <rfl.hpp>
#include <state/serialised_config.hpp>

namespace rfl {

using namespace wolf::core;
using namespace wolf::config;

template <> struct Reflector<events::PairSignal> {
  struct ReflType {
    std::string client_ip;
    std::string host_ip;
  };

  static events::PairSignal to(const ReflType &v) noexcept {
    return {.client_ip = v.client_ip,
            .host_ip = v.host_ip,
            .user_pin = std::make_shared<boost::promise<std::string>>()};
  }

  static ReflType from(const events::PairSignal &v) {
    return {.client_ip = v.client_ip, .host_ip = v.host_ip};
  }
};

template <> struct Reflector<events::App> {
  struct ReflType {
    const std::string title;
    const std::string id;
    // Make support_hdr optional to match TOML config behavior
    std::optional<bool> support_hdr;
    std::optional<std::string> icon_png_path;

    // Make GStreamer pipeline fields optional to match TOML config behavior
    // This fixes the "Field not found" errors when these are omitted (like XFCE config)
    std::optional<std::string> h264_gst_pipeline;
    std::optional<std::string> hevc_gst_pipeline;
    std::optional<std::string> av1_gst_pipeline;

    std::optional<std::string> render_node;
    std::optional<std::string> video_producer_buffer_caps;

    std::optional<std::string> opus_gst_pipeline;
    // Make compositor/audio optional to match TOML config behavior
    std::optional<bool> start_virtual_compositor;
    std::optional<bool> start_audio_server;
    rfl::TaggedUnion<"type", AppCMD, AppDocker, AppChildSession> runner;
  };

  static ReflType from(const events::App &v) {
    return {.title = v.base.title,
            .id = v.base.id,
            .support_hdr = v.base.support_hdr,
            .icon_png_path = v.base.icon_png_path,
            .h264_gst_pipeline = v.h264_gst_pipeline,
            .hevc_gst_pipeline = v.hevc_gst_pipeline,
            .av1_gst_pipeline = v.av1_gst_pipeline,
            .render_node = v.render_node,
            .video_producer_buffer_caps = v.video_producer_buffer_caps,
            .opus_gst_pipeline = v.opus_gst_pipeline,
            .start_virtual_compositor = v.start_virtual_compositor,
            .start_audio_server = v.start_audio_server,
            .runner = v.runner->serialize()};
  }
};

template <> struct Reflector<events::StartRunner> {
  struct ReflType {
    bool stop_stream_when_over;
    rfl::TaggedUnion<"type", AppCMD, AppDocker, AppChildSession> runner;
    std::string session_id;
  };

  static ReflType from(const events::StartRunner &v) {
    return {.stop_stream_when_over = v.stop_stream_when_over,
            .runner = v.runner->serialize(),
            .session_id = std::to_string(v.stream_session->session_id)};
  }
};

template <> struct Reflector<events::StreamSession> {
  struct ReflType {
    std::string app_id;
    std::string client_id;
    std::string client_ip;

    // gcm encryption keys
    std::string aes_key;
    std::string aes_iv;
    std::string rtsp_fake_ip;

    int video_width;
    int video_height;
    int video_refresh_rate;

    int audio_channel_count;

    wolf::config::ClientSettings client_settings;
  };

  static ReflType from(const events::StreamSession &v) {
    return {.app_id = v.app->base.id,
            .client_id = std::to_string(v.session_id),
            .client_ip = v.ip,
            .aes_key = v.aes_key,
            .aes_iv = v.aes_iv,
            .rtsp_fake_ip = v.rtsp_fake_ip,
            .video_width = v.display_mode.width,
            .video_height = v.display_mode.height,
            .video_refresh_rate = v.display_mode.refreshRate,
            .audio_channel_count = v.audio_channel_count,
            .client_settings = v.client_settings};
  }
};

} // namespace rfl