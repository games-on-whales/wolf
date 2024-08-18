#pragma once

#include <chrono>
#include <core/audio.hpp>
#include <core/input.hpp>
#include <core/virtual-display.hpp>
#include <eventbus/event_bus.hpp>
#include <gst/gst.h>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <memory>
#include <optional>

namespace state {

enum ColorRange {
  JPEG,
  MPEG
};

enum ColorSpace : int {
  BT601,
  BT709,
  BT2020
};

/**
 * A VideoSession is created after the param exchange over RTSP
 */
struct VideoSession {
  wolf::core::virtual_display::DisplayMode display_mode;
  std::string gst_pipeline;

  // A unique ID that identifies this session
  std::size_t session_id;

  std::uint16_t port;
  std::chrono::milliseconds timeout;

  int packet_size;
  int frames_with_invalid_ref_threshold;
  int fec_percentage;
  int min_required_fec_packets;
  long bitrate_kbps;
  int slices_per_frame;

  ColorRange color_range;
  ColorSpace color_space;

  std::string client_ip;
};

using namespace wolf::core::audio;

struct AudioSession {
  std::string gst_pipeline;

  // A unique ID that identifies this session
  std::size_t session_id;

  bool encrypt_audio;
  std::string aes_key;
  std::string aes_iv;

  std::uint16_t port;
  std::string client_ip;

  int packet_duration;
  AudioMode audio_mode;
};

/**
 * This event will trigger the start of the application command
 */
struct SocketReadyEV {
  std::size_t session_id;

  std::string wayland_socket;
  std::string xorg_socket;
};

} // namespace state