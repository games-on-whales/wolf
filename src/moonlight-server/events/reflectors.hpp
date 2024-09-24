#pragma once

#include <events/events.hpp>
#include <rfl.hpp>
#include <state/serialised_config.hpp>

namespace rfl {

using namespace wolf::core;
using namespace wolf::config;

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

template <> struct Reflector<events::App> {
  struct ReflType {
    const std::string title;
    const std::string id;
    const bool support_hdr;

    std::string h264_gst_pipeline;
    std::string hevc_gst_pipeline;
    std::string av1_gst_pipeline;

    std::string render_node;

    std::string opus_gst_pipeline;
    bool start_virtual_compositor;
    rfl::TaggedUnion<"type", AppCMD, AppDocker> runner;
    ControllerType joypad_type;
  };

  static ReflType from(const events::App &v) {
    ControllerType ctrl_type;
    switch (v.joypad_type) {
    case moonlight::control::pkts::CONTROLLER_TYPE::XBOX:
      ctrl_type = ControllerType::XBOX;
      break;
    case moonlight::control::pkts::CONTROLLER_TYPE::PS:
      ctrl_type = ControllerType::PS;
      break;
    case moonlight::control::pkts::CONTROLLER_TYPE::NINTENDO:
      ctrl_type = ControllerType::NINTENDO;
      break;
    case moonlight::control::pkts::CONTROLLER_TYPE::AUTO:
    case moonlight::control::pkts::UNKNOWN:
      ctrl_type = ControllerType::AUTO;
      break;
    }
    return {.title = v.base.title,
            .id = v.base.id,
            .support_hdr = v.base.support_hdr,
            .h264_gst_pipeline = v.h264_gst_pipeline,
            .hevc_gst_pipeline = v.hevc_gst_pipeline,
            .av1_gst_pipeline = v.av1_gst_pipeline,
            .render_node = v.render_node,
            .opus_gst_pipeline = v.opus_gst_pipeline,
            .start_virtual_compositor = v.start_virtual_compositor,
            .runner = v.runner->serialize(),
            .joypad_type = ctrl_type};
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

} // namespace rfl