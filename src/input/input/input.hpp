#pragma once

#include "moonlight/data-structures.hpp"
#include <cstdint>
#include <eventbus/event_bus.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <moonlight/data-structures.hpp>
#include <thread>

namespace input {

/**
 * PLATFORM DEPENDENT
 * will wait for events on the event bus and setup virtual devices accordingly.
 */
immer::array<immer::box<dp::handler_registration>> setup_handlers(std::size_t session_id,
                                                                  std::shared_ptr<dp::event_bus> event_bus);

/**
 * A packet of type INPUT_DATA will have different shapes based on the type
 */
namespace data {

enum INPUT_TYPE : int {
  MOUSE_MOVE_REL = 0x08,
  MOUSE_MOVE_ABS = 0x0e,
  MOUSE_BUTTON = 0x05,

  KEYBOARD_OR_SCROLL = 0x0A,

  CONTROLLER_MULTI = 0x1E,
  CONTROLLER = 0x18
};

constexpr int KEYBOARD_BUTTON_RELEASED = 0x04;
constexpr int MOUSE_BUTTON_RELEASED = 0x09;

struct INPUT_PKT {
  unsigned short packet_type; // This should always be 0x0206 little endian (INPUT_DATA)
  INPUT_TYPE type;
};

struct MOUSE_MOVE_REL_PACKET : INPUT_PKT {
  int magic;
  short delta_x;
  short delta_y;
};

struct MOUSE_MOVE_ABS_PACKET : INPUT_PKT {
  int magic;
  short x;
  short y;
  short unused;
  short width;
  short height;
};

struct MOUSE_BUTTON_PACKET : INPUT_PKT {
  char action;
  int button;
};

struct MOUSE_SCROLL_PACKET : INPUT_PKT {
  char magic_a; // static: 0x0A
  char zero1;
  short zero2;
  short scroll_amt1;
  short scroll_amt2;
  short zero3;
};

struct KEYBOARD_PACKET : INPUT_PKT {
  char key_action;
  short zero1;
  short key_code;
  short magic;
  char modifiers;
  short zero2;
};

struct CONTROLLER_MULTI_PACKET : INPUT_PKT {
  int header_a;
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

struct CONTROLLER_PACKET : INPUT_PKT {
  int header_a;
  short header_b;
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

} // namespace data

} // namespace input