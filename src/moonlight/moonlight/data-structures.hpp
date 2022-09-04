#pragma once
#include <string>

namespace moonlight {

struct DisplayMode {
  int width;
  int height;
  int refreshRate;
  bool hevc_supported = false;
};

struct App {
  const std::string title;
  const std::string id;
  const bool support_hdr;
};

namespace control {

enum PACKET_TYPE {
  START_A = 0x0305,
  START_B = 0x0307,
  INVALIDATE_REF_FRAMES = 0x0301,
  LOSS_STATS = 0x0201,
  FRAME_STATS = 0x0204,
  INPUT_DATA = 0x0206,
  RUMBLE_DATA = 0x010b,
  TERMINATION = 0x0100,
  PERIODIC_PING = 0x0200,
  IDR_FRAME = 0x0302,
  ENCRYPTED = 0x0001
};

/**
 * Events received in the ControlSession will be fired up in the event_bus
 */
struct ControlEvent {
  // A unique ID that identifies this session
  std::size_t session_id;

  PACKET_TYPE type;
  std::string_view raw_packet;
};

} // namespace control

} // namespace moonlight