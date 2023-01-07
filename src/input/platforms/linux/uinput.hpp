#include "input/input.hpp"
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

namespace input {

using libevdev_ptr = std::shared_ptr<libevdev>;
using libevdev_uinput_ptr = std::shared_ptr<libevdev_uinput>;

struct VirtualDevices {
  std::optional<libevdev_uinput_ptr> mouse;
  std::optional<libevdev_uinput_ptr> touchpad;
  std::optional<libevdev_uinput_ptr> keyboard;

  immer::array<libevdev_uinput_ptr> controllers{};
};

namespace mouse {

std::optional<libevdev_uinput *> create_mouse(libevdev *dev);

std::optional<libevdev_uinput *> create_touchpad(libevdev *dev);

void move_mouse(libevdev_uinput *mouse, const data::MOUSE_MOVE_REL_PACKET &move_pkt);

void move_touchpad(libevdev_uinput *mouse, const data::MOUSE_MOVE_ABS_PACKET &move_pkt);

void mouse_press(libevdev_uinput *mouse, const data::MOUSE_BUTTON_PACKET &btn_pkt);

void mouse_scroll(libevdev_uinput *mouse, const data::MOUSE_SCROLL_PACKET &scroll_pkt);
} // namespace mouse

namespace keyboard {

std::optional<libevdev_uinput *> create_keyboard(libevdev *dev);

void keyboard_handle(libevdev_uinput *keyboard, const data::KEYBOARD_PACKET &key_pkt);

} // namespace keyboard

namespace controller {

std::optional<libevdev_uinput *> create_controller(libevdev *dev);

void controller_handle(libevdev_uinput *controller,
                       const data::CONTROLLER_MULTI_PACKET &ctrl_pkt,
                       const data::CONTROLLER_MULTI_PACKET &prev_ctrl_pkt);

} // namespace controller

} // namespace input