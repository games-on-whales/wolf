#pragma once

#include <chrono>
#include <eventbus/event_bus.hpp>
#include <moonlight-common-c/src/Video.h>
#include <moonlight/data-structures.hpp>

namespace state {

/**
 * A VideoSession is created after the param exchange over RTSP
 */
struct VideoSession : moonlight::DisplayMode {
  // A unique ID that identifies this session
  std::size_t session_id;
  std::shared_ptr<dp::event_bus> event_bus;

  std::uint16_t port;
  std::chrono::milliseconds timeout;

  int packet_size;
  int frames_with_invalid_ref_threshold;
  int fec_percentage;
  int min_required_fec_packets;
  int bitrate_kbps;

  std::string client_ip;
};

struct AudioSession {
  // A unique ID that identifies this session
  std::size_t session_id;
  std::shared_ptr<dp::event_bus> event_bus;

  bool encrypt_audio;
  std::string gcm_key;
  std::string gcm_iv;

  std::uint16_t port;
  std::string client_ip;

  int fec_percentage;
  int min_required_fec_packets;
  int packetDuration;
  int channels;
  int mask;
  int bitrate = 48000;
};

struct VideoRTPHeaders {
  // headers
  RTP_PACKET rtp;
  char reserved[4];
  NV_VIDEO_PACKET packet;
};

struct AudioRTPHeaders {
  RTP_PACKET rtp;
};

struct AudioFECHeader {
  uint8_t fecShardIndex;
  uint8_t payloadType;
  uint16_t baseSequenceNumber;
  uint32_t baseTimestamp;
  uint32_t ssrc;
};

struct AudioFECPacket {
  RTP_PACKET rtp;
  AudioFECHeader fec_header;
};

} // namespace state