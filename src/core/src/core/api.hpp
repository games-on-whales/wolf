#pragma once
#include <boost/endian/conversion.hpp>
#include <string>
#include <utility>

namespace wolf::core::api {

enum PACKET_TYPE : std::uint16_t {
  START_A = boost::endian::little_to_native(0x0305),
  START_B = boost::endian::little_to_native(0x0307),
  INVALIDATE_REF_FRAMES = boost::endian::little_to_native(0x0301),
  LOSS_STATS = boost::endian::little_to_native(0x0201),
  FRAME_STATS = boost::endian::little_to_native(0x0204),
  INPUT_DATA = boost::endian::little_to_native(0x0206),
  RUMBLE_DATA = boost::endian::little_to_native(0x010b),
  TERMINATION = boost::endian::little_to_native(0x0109),
  PERIODIC_PING = boost::endian::little_to_native(0x0200),
  IDR_FRAME = boost::endian::little_to_native(0x0302),
  ENCRYPTED = boost::endian::little_to_native(0x0001)
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

struct PauseStreamEvent {
  std::size_t session_id;
};

struct ResumeStreamEvent {
  std::size_t session_id;
};

struct StopStreamEvent {
  std::size_t session_id;
};

struct DisplayMode {
  int width;
  int height;
  int refreshRate;
};

} // namespace wolf::core::api