#pragma once

#include <chrono>
#include <eventbus/event_bus.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <input/input.hpp>
#include <memory>
#include <moonlight/data-structures.hpp>
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
  moonlight::DisplayMode display_mode;
  std::string gst_pipeline;

  immer::box<input::InputReady> virtual_inputs;

  // A unique ID that identifies this session
  std::size_t session_id;

  std::uint16_t port;
  std::chrono::milliseconds timeout;

  int packet_size;
  int frames_with_invalid_ref_threshold;
  int fec_percentage;
  int min_required_fec_packets;
  int bitrate_kbps;
  int slices_per_frame;

  ColorRange color_range;
  ColorSpace color_space;

  std::string client_ip;

  /**
   * The full command to be launched
   */
  std::optional<std::string> app_launch_cmd;
};

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
  int channels;
  int bitrate = 48000;
};

/**
 * This event will trigger the start of the application command
 */
struct LaunchAPPEvent {
  std::size_t session_id;
  std::shared_ptr<dp::event_bus> event_bus;

  /**
   * The full command to be launched
   */
  std::string app_launch_cmd;

  std::optional<std::string> wayland_socket;
  std::optional<std::string> xorg_socket;
};

struct VideoRTPHeaders {
  // headers
  moonlight::RTP_PACKET rtp;
  char reserved[4];
  moonlight::NV_VIDEO_PACKET packet;
};

struct AudioRTPHeaders {
  moonlight::RTP_PACKET rtp;
};

struct AudioFECHeader {
  uint8_t fecShardIndex;
  uint8_t payloadType;
  uint16_t baseSequenceNumber;
  uint32_t baseTimestamp;
  uint32_t ssrc;
};

struct AudioFECPacket {
  moonlight::RTP_PACKET rtp;
  AudioFECHeader fec_header;
};

} // namespace state