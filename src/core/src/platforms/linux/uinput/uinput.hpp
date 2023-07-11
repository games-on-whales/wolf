#include <core/input.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libudev.h>
#include <memory>
#include <optional>

namespace wolf::core::input {

using libevdev_ptr = std::shared_ptr<libevdev>;
using libevdev_uinput_ptr = std::shared_ptr<libevdev_uinput>;
using libevdev_event_ptr = std::shared_ptr<input_event>;

/**
 * Given a device will read all queued events available at this time up to max_events
 * It'll automatically discard all EV_SYN events
 *
 * @returns a list of smart pointers of evdev input_event (empty when no events are available)
 */
std::vector<libevdev_event_ptr> fetch_events(const libevdev_ptr &dev, int max_events = 50);

namespace mouse {

std::optional<libevdev_uinput *> create_mouse(libevdev *dev);

std::optional<libevdev_uinput *> create_mouse_abs(libevdev *dev);

void move_mouse(libevdev_uinput *mouse, const data::MOUSE_MOVE_REL_PACKET &move_pkt);

void move_mouse_abs(libevdev_uinput *mouse, const data::MOUSE_MOVE_ABS_PACKET &move_pkt);

void mouse_press(libevdev_uinput *mouse, const data::MOUSE_BUTTON_PACKET &btn_pkt);

void mouse_scroll(libevdev_uinput *mouse, const data::MOUSE_SCROLL_PACKET &scroll_pkt);

void mouse_scroll_horizontal(libevdev_uinput *mouse, const data::MOUSE_HSCROLL_PACKET &scroll_pkt);
} // namespace mouse

namespace keyboard {

std::optional<libevdev_uinput *> create_keyboard(libevdev *dev);

struct Action {
  bool pressed;
  int linux_code;
};

std::optional<Action> keyboard_handle(libevdev_uinput *keyboard, const data::KEYBOARD_PACKET &key_pkt);
void paste_utf(libevdev_uinput *kb, const data::UTF8_TEXT_PACKET &pkt);
std::string to_hex(const std::basic_string<char32_t> &str);

} // namespace keyboard

namespace controller {

struct Controller {
  libevdev_uinput_ptr uinput;
  std::shared_ptr<immer::atom<immer::box<data::CONTROLLER_MULTI_PACKET>>> prev_pkt;
};

std::optional<libevdev_uinput *> create_controller(libevdev *dev, data::CONTROLLER_TYPE type, uint8_t capabilities);

void controller_handle(libevdev_uinput *controller,
                       const data::CONTROLLER_MULTI_PACKET &ctrl_pkt,
                       const data::CONTROLLER_MULTI_PACKET &prev_ctrl_pkt);

} // namespace controller

struct VirtualDevices {
  std::optional<libevdev_uinput_ptr> mouse;
  std::optional<libevdev_uinput_ptr> mouse_abs;
  std::optional<libevdev_uinput_ptr> keyboard;

  immer::array<controller::Controller> controllers{};
};

} // namespace wolf::core::input
