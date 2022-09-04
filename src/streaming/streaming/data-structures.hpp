#pragma once

#include <chrono>
#include <eventbus/event_bus.hpp>
#include <gst/gst.h>
#include <immer/box.hpp>
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

struct VideoRTPHeaders {
  // headers
  RTP_PACKET rtp;
  char reserved[4];
  NV_VIDEO_PACKET packet;
};

} // namespace state