/**
 * This is all based on libevdev
 *  - Here's a great introductory blog post: https://web.archive.org/web/20200809000852/https://who-t.blogspot.com/2016/09/understanding-evdev.html/
 *  - Main docs: https://www.freedesktop.org/software/libevdev/doc/latest/index.html
 *  - Python docs are also of good quality: https://python-libevdev.readthedocs.io/en/latest/index.html
 *
 * You can debug your system using `evemu-describe` and `evemu-record`
 * (they can be installed using: `apt install -y evemu-tools`)
 */

#include <boost/endian/conversion.hpp>
#include <helpers/logger.hpp>
#include <input/input.hpp>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <optional>

namespace input {

using namespace moonlight::control;

using libevdev_ptr = std::shared_ptr<libevdev>;
using libevdev_uinput_ptr = std::shared_ptr<libevdev_uinput>;

constexpr int ABS_MAX_WIDTH = 1920;
constexpr int ABS_MAX_HEIGHT = 1080;

std::optional<libevdev_uinput *> create_mouse(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Mouse");
  libevdev_set_name(dev, "Wolf mouse virtual device");
  libevdev_set_id_vendor(dev, 0xAB01);
  libevdev_set_id_product(dev, 0xAB02);
  libevdev_set_id_version(dev, 0xAB03);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_SIDE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_EXTRA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_FORWARD, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_BACK, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TASK, nullptr);

  libevdev_enable_event_type(dev, EV_REL);
  libevdev_enable_event_code(dev, EV_REL, REL_X, nullptr);
  libevdev_enable_event_code(dev, EV_REL, REL_Y, nullptr);

  libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, nullptr);
  libevdev_enable_event_code(dev, EV_REL, REL_WHEEL_HI_RES, nullptr);
  libevdev_enable_event_code(dev, EV_REL, REL_HWHEEL, nullptr);
  libevdev_enable_event_code(dev, EV_REL, REL_HWHEEL_HI_RES, nullptr);

  libevdev_enable_event_type(dev, EV_MSC);
  libevdev_enable_event_code(dev, EV_MSC, MSC_SCAN, nullptr);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", err);
    return {};
  }

  return uidev;
}

std::optional<libevdev_uinput *> create_touchpad(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Touchpad");
  libevdev_set_name(dev, "Wolf touchpad virtual device");
  libevdev_set_id_vendor(dev, 0xAB11);
  libevdev_set_id_product(dev, 0xAB12);
  libevdev_set_id_version(dev, 0xAB13);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_property(dev, INPUT_PROP_DIRECT);
  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOUCH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_PEN, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_FINGER, nullptr);

  struct input_absinfo absinfo {
    .value = 0, .minimum = 0, .maximum = 0, .fuzz = 1, .flat = 0, .resolution = 40
  };
  libevdev_enable_event_type(dev, EV_ABS);

  absinfo.maximum = ABS_MAX_WIDTH;
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &absinfo);
  absinfo.maximum = ABS_MAX_HEIGHT;
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &absinfo);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", err);
    return {};
  }

  return uidev;
}

std::optional<libevdev_uinput *> create_keyboard(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Keyboard");
  libevdev_set_name(dev, "Wolf keyboard virtual device");
  libevdev_set_id_vendor(dev, 0xAB21);
  libevdev_set_id_product(dev, 0xAB22);
  libevdev_set_id_version(dev, 0xAB33);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, KEY_BACKSPACE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_TAB, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_CLEAR, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_ENTER, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTSHIFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTCTRL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTALT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_PAUSE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_CAPSLOCK, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KATAKANAHIRAGANA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_HANGEUL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_HANJA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KATAKANA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_ESC, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SPACE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_PAGEUP, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_PAGEDOWN, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_END, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_HOME, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_UP, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_DOWN, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SELECT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_PRINT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SYSRQ, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_INSERT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_DELETE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_HELP, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_0, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_1, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_2, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_3, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_4, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_5, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_6, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_7, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_8, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_9, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_A, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_B, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_C, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_D, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_E, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_F, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_G, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_H, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_I, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_J, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_K, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_L, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_M, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_N, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_O, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_P, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_Q, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_R, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_S, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_T, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_U, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_V, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_W, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_X, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_Y, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_Z, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTMETA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHTMETA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SLEEP, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP0, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP1, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP2, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP3, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP4, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP5, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP6, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP7, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP8, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KP9, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KPASTERISK, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KPPLUS, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KPCOMMA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KPMINUS, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KPDOT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_KPSLASH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_F11, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_F12, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_NUMLOCK, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SCROLLLOCK, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTSHIFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHTSHIFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTCTRL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHTCTRL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTALT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHTALT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SEMICOLON, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_EQUAL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_COMMA, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_MINUS, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_DOT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_SLASH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_GRAVE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTBRACE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_BACKSLASH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHTBRACE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_APOSTROPHE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, KEY_102ND, nullptr);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", err);
    return {};
  }

  return uidev;
}

void move_mouse(libevdev_uinput *mouse, const data::MOUSE_MOVE_REL_PACKET &move_pkt) {
  if (move_pkt.delta_x) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_X, move_pkt.delta_x);
  }

  if (move_pkt.delta_y) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_Y, move_pkt.delta_y);
  }

  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void move_touchpad(libevdev_uinput *mouse, const data::MOUSE_MOVE_ABS_PACKET &move_pkt) {
  float x = boost::endian::big_to_native(move_pkt.x);
  float y = boost::endian::big_to_native(move_pkt.y);
  float width = boost::endian::big_to_native(move_pkt.width);
  float height = boost::endian::big_to_native(move_pkt.height);

  int scaled_x = (int)std::lround((ABS_MAX_WIDTH / width) * x);
  int scaled_y = (int)std::lround((ABS_MAX_HEIGHT / height) * y);

  libevdev_uinput_write_event(mouse, EV_ABS, ABS_X, scaled_x);
  libevdev_uinput_write_event(mouse, EV_ABS, ABS_Y, scaled_y);
  libevdev_uinput_write_event(mouse, EV_KEY, BTN_TOOL_FINGER, 1);
  libevdev_uinput_write_event(mouse, EV_KEY, BTN_TOOL_FINGER, 0);

  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void mouse_press(libevdev_uinput *mouse, const data::MOUSE_BUTTON_PACKET &btn_pkt) {
  int btn_type;
  int scan;
  auto release = btn_pkt.action == data::MOUSE_BUTTON_RELEASED;

  if (btn_pkt.button == 1) {
    btn_type = BTN_LEFT;
    scan = 90001;
  } else if (btn_pkt.button == 2) {
    btn_type = BTN_MIDDLE;
    scan = 90003;
  } else if (btn_pkt.button == 3) {
    btn_type = BTN_RIGHT;
    scan = 90002;
  } else if (btn_pkt.button == 4) {
    btn_type = BTN_SIDE;
    scan = 90004;
  } else {
    btn_type = BTN_EXTRA;
    scan = 90005;
  }

  libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan);
  libevdev_uinput_write_event(mouse, EV_KEY, btn_type, release ? 0 : 1);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void mouse_scroll(libevdev_uinput *mouse, const data::MOUSE_SCROLL_PACKET &scroll_pkt) {
  int high_res_distance = scroll_pkt.scroll_amt1;
  int distance = high_res_distance / 120;

  libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL, distance);
  libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL_HI_RES, high_res_distance);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void keyboard(libevdev_uinput *keyboard, const data::KEYBOARD_PACKET &key_pkt) {
  auto release = key_pkt.key_action == data::KEYBOARD_BUTTON_RELEASED;

  logs::log(logs::trace, "[INPUT] keyboard: code: {}, release?: {}", key_pkt.key_code, release);
}

immer::array<immer::box<dp::handler_registration>> setup_handlers(std::size_t session_id,
                                                                  std::shared_ptr<dp::event_bus> event_bus) {
  logs::log(logs::debug, "Setting up input handlers for session: {}", session_id);

  libevdev_ptr mouse_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr mouse_ptr(create_mouse(mouse_dev.get()).value(),
                                ::libevdev_uinput_destroy); // TODO: handle unable to create mouse

  libevdev_ptr touch_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr touch_ptr(create_touchpad(touch_dev.get()).value(),
                                ::libevdev_uinput_destroy); // TODO: handle unable to create touchpad

  libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr keyboard_ptr(create_keyboard(keyboard_dev.get()).value(),
                                   ::libevdev_uinput_destroy); // TODO: handle unable to create touchpad

  auto ctrl_handler = event_bus->register_handler<immer::box<ControlEvent>>(
      [sess_id = session_id, mouse_ptr, touch_ptr, keyboard_ptr](immer::box<ControlEvent> ctrl_ev) {
        if (ctrl_ev->session_id == sess_id && ctrl_ev->type == INPUT_DATA) {
          auto input = (const data::INPUT_PKT *)(ctrl_ev->raw_packet.data());

          switch ((int)boost::endian::big_to_native((int)input->type)) {
          case data::MOUSE_MOVE_REL:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_REL");
            move_mouse(mouse_ptr.get(), *(data::MOUSE_MOVE_REL_PACKET *)input);
            break;
          case data::MOUSE_MOVE_ABS:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_ABS");
            move_touchpad(touch_ptr.get(), *(data::MOUSE_MOVE_ABS_PACKET *)input);
            break;
          case data::MOUSE_BUTTON:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_BUTTON");
            mouse_press(mouse_ptr.get(), *(data::MOUSE_BUTTON_PACKET *)input);
            break;
          case data::KEYBOARD_OR_SCROLL: {
            char *sub_input_type = (char *)input + 8;
            if (sub_input_type[0] == data::KEYBOARD_OR_SCROLL) {
              logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_SCROLL_PACKET");
              mouse_scroll(mouse_ptr.get(), *(data::MOUSE_SCROLL_PACKET *)input);
            } else {
              logs::log(logs::trace, "[INPUT] Received input of type: KEYBOARD_PACKET");
              keyboard(keyboard_ptr.get(), *(data::KEYBOARD_PACKET *)input);
            }
            break;
          }
          case data::CONTROLLER_MULTI:
            logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MULTI");
            break;
          case data::CONTROLLER:
            logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER");
            break;
          }
        }
      });

  return immer::array<immer::box<dp::handler_registration>>{std::move(ctrl_handler)};
}
} // namespace input