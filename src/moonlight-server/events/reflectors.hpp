#pragma once

#include <events/events.hpp>
#include <rfl.hpp>

namespace rfl {

using namespace wolf::core;

template <> struct Reflector<events::PairSignal> {
  struct ReflType {
    const std::string event_type = "pair";
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

template <> struct Reflector<std::chrono::milliseconds> {
  struct ReflType {
    int64_t milliseconds;
  };

  static std::chrono::milliseconds to(const ReflType &v) noexcept {
    return std::chrono::milliseconds(v.milliseconds);
  }

  static ReflType from(const std::chrono::milliseconds &v) {
    return {.milliseconds = v.count()};
  }
};

template <> struct Reflector<wolf::core::audio::AudioMode::Speakers> {
  struct ReflType {
    int speakers;
  };

  static wolf::core::audio::AudioMode::Speakers to(const ReflType &v) noexcept {
    return static_cast<wolf::core::audio::AudioMode::Speakers>(v.speakers);
  }

  static ReflType from(const wolf::core::audio::AudioMode::Speakers &v) {
    return {.speakers = static_cast<int>(v)};
  }
};

template <> struct Reflector<events::App> {
  struct ReflType {
    moonlight::App base;

    std::string h264_gst_pipeline;
    std::string hevc_gst_pipeline;
    std::string av1_gst_pipeline;

    std::string render_node;

    std::string opus_gst_pipeline;
    bool start_virtual_compositor;
    // TODO: Runner
    int joypad_type;
  };

  static events::App to(const ReflType &v) noexcept {
    return {.base = v.base,
            .h264_gst_pipeline = v.h264_gst_pipeline,
            .hevc_gst_pipeline = v.hevc_gst_pipeline,
            .av1_gst_pipeline = v.av1_gst_pipeline,
            .render_node = v.render_node,
            .opus_gst_pipeline = v.opus_gst_pipeline,
            .start_virtual_compositor = v.start_virtual_compositor,
            .joypad_type = static_cast<moonlight::control::pkts::CONTROLLER_TYPE>(v.joypad_type)};
  }

  static ReflType from(const events::App &v) {
    return {.base = v.base,
            .h264_gst_pipeline = v.h264_gst_pipeline,
            .hevc_gst_pipeline = v.hevc_gst_pipeline,
            .av1_gst_pipeline = v.av1_gst_pipeline,
            .render_node = v.render_node,
            .opus_gst_pipeline = v.opus_gst_pipeline,
            .start_virtual_compositor = v.start_virtual_compositor,
            .joypad_type = v.joypad_type};
  }
};

template <> struct Reflector<events::StreamSession> {
  struct ReflType {
    const std::string event_type = "stream_session";

    moonlight::DisplayMode display_mode;
    int audio_channel_count;

    std::shared_ptr<events::App> app;
    std::string app_state_folder;

    // gcm encryption keys
    std::string aes_key;
    std::string aes_iv;

    // client info
    std::size_t session_id;
    std::string ip;
  };

  static events::StreamSession to(const ReflType &v) noexcept {
    return {.display_mode = v.display_mode,
            .audio_channel_count = v.audio_channel_count,
            .app = v.app,
            .app_state_folder = v.app_state_folder,
            .aes_key = v.aes_key,
            .aes_iv = v.aes_iv,
            .session_id = v.session_id,
            .ip = v.ip,
            .wayland_display = std::make_shared<immer::atom<virtual_display::wl_state_ptr>>(),
            .mouse = std::make_shared<std::optional<events::MouseTypes>>(),
            .keyboard = std::make_shared<std::optional<events::KeyboardTypes>>(),
            .joypads = std::make_shared<immer::atom<events::JoypadList>>(),
            .pen_tablet = std::make_shared<std::optional<input::PenTablet>>(),
            .touch_screen = std::make_shared<std::optional<input::TouchScreen>>()};
  }

  // TODO: can't use this in practice, missing event_bus and App.runner
  static ReflType from(const events::StreamSession &v) {
    return {.display_mode = v.display_mode,
            .audio_channel_count = v.audio_channel_count,
            .app = v.app,
            .app_state_folder = v.app_state_folder,
            .aes_key = v.aes_key,
            .aes_iv = v.aes_iv,
            .session_id = v.session_id,
            .ip = v.ip};
  }
};

template <> struct Reflector<events::ControlEvent> {
  struct ReflType {
    const std::string event_type = "control_event";
    std::size_t session_id;
    int type;
  };

  static events::ControlEvent to(const ReflType &v) noexcept {
    return {.session_id = v.session_id, .type = static_cast<moonlight::control::pkts::PACKET_TYPE>(v.type)};
  }

  // TODO: this doesn't make sense without raw_packet
  static ReflType from(const events::ControlEvent &v) {
    return {.session_id = v.session_id, .type = static_cast<int>(v.type)};
  }
};

} // namespace rfl