#pragma once

#include "moonlight/data-structures.hpp"
#include <boost/asio.hpp>
#include <boost/endian.hpp>
#include <cstdint>
#include <eventbus/event_bus.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <moonlight/data-structures.hpp>
#include <thread>

namespace input {

struct InputReady {
  immer::array<std::string> devices_paths;
  immer::array<immer::box<dp::handler_registration>> registered_handlers;
};

/**
 * PLATFORM DEPENDENT
 * will wait for events on the event bus and setup virtual devices accordingly.
 */
InputReady setup_handlers(std::size_t session_id, const std::shared_ptr<dp::event_bus> &event_bus);

/**
 * A packet of type INPUT_DATA will have different shapes based on the type
 */
namespace data {

enum INPUT_TYPE : int {
  MOUSE_MOVE_REL = boost::endian::native_to_little(0x00000007),
  MOUSE_MOVE_ABS = boost::endian::native_to_little(0x00000005),
  MOUSE_BUTTON_PRESS = boost::endian::native_to_little(0x00000008),
  MOUSE_BUTTON_RELEASE = boost::endian::native_to_little(0x00000009),
  KEY_PRESS = boost::endian::native_to_little(0x00000003),
  KEY_RELEASE = boost::endian::native_to_little(0x00000004),
  MOUSE_SCROLL = boost::endian::native_to_little(0x0000000A),
  MOUSE_HSCROLL = boost::endian::native_to_little(0x55000001),
  CONTROLLER_MULTI = boost::endian::native_to_little(0x0000000C),
  UTF8_TEXT = boost::endian::native_to_little(0x00000017),
};

enum CONTROLLER_BTN : unsigned short {
  DPAD_UP = 0x0001,
  DPAD_DOWN = 0x0002,
  DPAD_LEFT = 0x0004,
  DPAD_RIGHT = 0x0008,

  START = 0x0010,
  BACK = 0x0020,
  HOME = 0x0400,

  LEFT_STICK = 0x0040,
  RIGHT_STICK = 0x0080,
  LEFT_BUTTON = 0x0100,
  RIGHT_BUTTON = 0x0200,

  A = 0x1000,
  B = 0x2000,
  X = 0x4000,
  Y = 0x8000
};

// make sure these structs are allocated in 1-byte blocks so the data aligns
// right
#pragma pack(push, 1)

struct INPUT_PKT {
  unsigned short packet_type; // This should always be 0x0206 little endian (INPUT_DATA)
  unsigned short packet_len;  // the total size of the packet

  unsigned int data_size; // the size of the input data

  INPUT_TYPE type;
};

struct MOUSE_MOVE_REL_PACKET : INPUT_PKT {
  short delta_x;
  short delta_y;
};

struct MOUSE_MOVE_ABS_PACKET : INPUT_PKT {
  short x;
  short y;
  short unused;
  short width;
  short height;
};

struct MOUSE_BUTTON_PACKET : INPUT_PKT {
  unsigned char button;
};

struct MOUSE_SCROLL_PACKET : INPUT_PKT {
  short scroll_amt1;
  short scroll_amt2;
  short zero1;
};

struct MOUSE_HSCROLL_PACKET : INPUT_PKT {
  short scroll_amount;
};

struct KEYBOARD_PACKET : INPUT_PKT {
  unsigned char flags;
  short key_code;
  unsigned char modifiers;
  short zero1;
};

// this is the same size moonlight uses
constexpr int UTF8_TEXT_MAX_LEN = 32;
struct UTF8_TEXT_PACKET : INPUT_PKT {
  char text[UTF8_TEXT_MAX_LEN];
};

struct CONTROLLER_MULTI_PACKET : INPUT_PKT {
  short header_b;
  short controller_number;
  short active_gamepad_mask;
  short mid_b;
  short button_flags;
  unsigned char left_trigger;
  unsigned char right_trigger;
  short left_stick_x;
  short left_stick_y;
  short right_stick_x;
  short right_stick_y;
  int tail_a;
  short tail_b;
};

#pragma pack(pop)

} // namespace data

} // namespace input
