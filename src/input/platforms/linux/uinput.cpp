/**
 * This is all based on libevdev
 *  - Here's a great introductory blog post:
 * https://web.archive.org/web/20200809000852/https://who-t.blogspot.com/2016/09/understanding-evdev.html/
 *  - Main docs: https://www.freedesktop.org/software/libevdev/doc/latest/index.html
 *  - Python docs are also of good quality: https://python-libevdev.readthedocs.io/en/latest/index.html
 *
 * You can debug your system using `evemu-describe`, `evemu-record` and `udevadm monitor`
 * (they can be installed using: `apt install -y evemu-tools`)
 */

#include "uinput.hpp"
#include "keyboard.hpp"
#include <boost/endian/conversion.hpp>
#include <chrono>
#include <helpers/logger.hpp>
#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/atom.hpp>
#include <range/v3/view.hpp>
#include <thread>

namespace input {

using namespace std::chrono_literals;
using namespace moonlight::control;

constexpr int ABS_MAX_WIDTH = 1920;
constexpr int ABS_MAX_HEIGHT = 1080;

namespace mouse {

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

  logs::log(logs::debug, "[INPUT] Created virtual mouse {}", libevdev_uinput_get_devnode(uidev));

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

  logs::log(logs::debug, "[INPUT] Created virtual touchpad {}", libevdev_uinput_get_devnode(uidev));

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

} // namespace mouse

namespace keyboard {

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

  for (auto ev : keyboard::key_mappings) {
    libevdev_enable_event_code(dev, EV_KEY, ev.second.linux_code, nullptr);
  }

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", err);
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual keyboard {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

void keyboard_repeat_press(libevdev_uinput *keyboard, const immer::array<int> &linux_codes) {
  for (auto code : linux_codes) {
    libevdev_uinput_write_event(keyboard, EV_KEY, code, 2);
    libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);
  }
}

std::optional<Action> keyboard_handle(libevdev_uinput *keyboard, const data::KEYBOARD_PACKET &key_pkt) {
  auto release = key_pkt.key_action == data::KEYBOARD_BUTTON_RELEASED;
  auto moonlight_key = (short)boost::endian::big_to_native((short)key_pkt.key_code);

  logs::log(logs::trace, "[INPUT] keyboard: code: {}, release?: {}", moonlight_key, release);

  auto search_key = keyboard::key_mappings.find(moonlight_key);
  if (search_key == keyboard::key_mappings.end()) {
    logs::log(logs::warning,
              "[INPUT] Moonlight sent keyboard code {} which is not recognised; ignoring.",
              moonlight_key);
    return {};
  } else {
    auto mapped_key = search_key->second;
    if (mapped_key.scan_code != keyboard::UNKNOWN && release) {
      libevdev_uinput_write_event(keyboard, EV_MSC, MSC_SCAN, mapped_key.scan_code);
    }

    libevdev_uinput_write_event(keyboard, EV_KEY, mapped_key.linux_code, release ? 0 : 1);
    libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);

    return Action{.pressed = !release, .linux_code = mapped_key.linux_code};
  }
}

} // namespace keyboard

namespace controller {

std::optional<libevdev_uinput *> create_controller(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf gamepad");
  libevdev_set_name(dev, "Wolf gamepad virtual device");
  libevdev_set_id_product(dev, 0xAB31);
  libevdev_set_id_vendor(dev, 0xAB32);
  libevdev_set_id_bustype(dev, 0xAB33);
  libevdev_set_id_version(dev, 0xAB34);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_WEST, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_EAST, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_NORTH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_SOUTH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_THUMBL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_THUMBR, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TR, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TL, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_SELECT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_MODE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_START, nullptr);

  input_absinfo stick{0, -32768, 32767, 16, 128, 0};
  input_absinfo trigger{0, 0, 255, 0, 0, 0};
  input_absinfo dpad{0, -1, 1, 0, 0, 0};
  libevdev_enable_event_type(dev, EV_ABS);
  libevdev_enable_event_code(dev, EV_ABS, ABS_HAT0Y, &dpad);
  libevdev_enable_event_code(dev, EV_ABS, ABS_HAT0X, &dpad);
  libevdev_enable_event_code(dev, EV_ABS, ABS_Z, &trigger);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RZ, &trigger);
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RX, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RY, &stick);

  libevdev_enable_event_type(dev, EV_FF);
  libevdev_enable_event_code(dev, EV_FF, FF_RUMBLE, nullptr);
  libevdev_enable_event_code(dev, EV_FF, FF_CONSTANT, nullptr);
  libevdev_enable_event_code(dev, EV_FF, FF_PERIODIC, nullptr);
  libevdev_enable_event_code(dev, EV_FF, FF_SINE, nullptr);
  libevdev_enable_event_code(dev, EV_FF, FF_RAMP, nullptr);
  libevdev_enable_event_code(dev, EV_FF, FF_GAIN, nullptr);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create controller device, error code: {}", err);
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual controller {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

using namespace input::data;

/**
 * The main trick here is that a single packet can contain multiple pressed buttons at the same time,
 * the way this is done is by setting multiple bits in the button_flags.
 * For example you can have both set to 1 `DPAD_UP` and `A`
 *
 * We also need to keep the previous packet in order to know if a button has been released.
 * Example: previous packet had `DPAD_UP` and `A` -> user release `A` -> new packet only has `DPAD_UP`
 */
void controller_handle(libevdev_uinput *controller,
                       const data::CONTROLLER_MULTI_PACKET &ctrl_pkt,
                       const data::CONTROLLER_MULTI_PACKET &prev_ctrl_pkt) {

  // Button flags that have been changed between current and prev
  auto bf_changed = ctrl_pkt.button_flags ^ prev_ctrl_pkt.button_flags;
  // Button flags that are only part of the new packet
  auto bf_new = ctrl_pkt.button_flags;

  if (bf_changed) {
    if ((DPAD_UP | DPAD_DOWN) & bf_changed) {
      int button_state = bf_new & DPAD_UP ? -1 : (bf_new & DPAD_DOWN ? 1 : 0);

      libevdev_uinput_write_event(controller, EV_ABS, ABS_HAT0Y, button_state);
    }

    if ((DPAD_LEFT | DPAD_RIGHT) & bf_changed) {
      int button_state = bf_new & DPAD_LEFT ? -1 : (bf_new & DPAD_RIGHT ? 1 : 0);

      libevdev_uinput_write_event(controller, EV_ABS, ABS_HAT0X, button_state);
    }

    if (START & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_START, bf_new & START ? 1 : 0);
    if (BACK & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_SELECT, bf_new & BACK ? 1 : 0);
    if (LEFT_STICK & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_THUMBL, bf_new & LEFT_STICK ? 1 : 0);
    if (RIGHT_STICK & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_THUMBR, bf_new & RIGHT_STICK ? 1 : 0);
    if (LEFT_BUTTON & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_TL, bf_new & LEFT_BUTTON ? 1 : 0);
    if (RIGHT_BUTTON & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_TR, bf_new & RIGHT_BUTTON ? 1 : 0);
    if (HOME & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_MODE, bf_new & HOME ? 1 : 0);
    if (A & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_SOUTH, bf_new & A ? 1 : 0);
    if (B & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_EAST, bf_new & B ? 1 : 0);
    if (X & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_NORTH, bf_new & X ? 1 : 0);
    if (Y & bf_changed)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_WEST, bf_new & Y ? 1 : 0);
  }

  if (ctrl_pkt.left_trigger != prev_ctrl_pkt.left_trigger) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_Z, ctrl_pkt.left_trigger);
  }

  if (ctrl_pkt.right_trigger != prev_ctrl_pkt.right_trigger) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_RZ, ctrl_pkt.right_trigger);
  }

  if (ctrl_pkt.left_stick_x != prev_ctrl_pkt.left_stick_x) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_X, ctrl_pkt.left_stick_x);
  }

  if (ctrl_pkt.left_stick_y != prev_ctrl_pkt.left_stick_y) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_Y, -ctrl_pkt.left_stick_y);
  }

  if (ctrl_pkt.right_stick_x != prev_ctrl_pkt.right_stick_x) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_RX, ctrl_pkt.right_stick_x);
  }

  if (ctrl_pkt.right_stick_y != prev_ctrl_pkt.right_stick_y) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_RY, -ctrl_pkt.right_stick_y);
  }

  libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
}

} // namespace controller

InputReady setup_handlers(std::size_t session_id, const std::shared_ptr<dp::event_bus> &event_bus) {
  logs::log(logs::debug, "Setting up input handlers for session: {}", session_id);

  auto v_devices = std::make_shared<VirtualDevices>();
  auto devices_paths = immer::array<immer::box<std::string>>().transient();

  libevdev_ptr mouse_dev(libevdev_new(), ::libevdev_free);
  if (auto mouse_el = mouse::create_mouse(mouse_dev.get())) {
    v_devices->mouse = {*mouse_el, ::libevdev_uinput_destroy};
    devices_paths.push_back(libevdev_uinput_get_devnode(*mouse_el));
  }

  libevdev_ptr touch_dev(libevdev_new(), ::libevdev_free);
  if (auto touch_el = mouse::create_touchpad(touch_dev.get())) {
    v_devices->touchpad = {*touch_el, ::libevdev_uinput_destroy};
    devices_paths.push_back(libevdev_uinput_get_devnode(*touch_el));
  }

  libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
  if (auto keyboard_el = keyboard::create_keyboard(keyboard_dev.get())) {
    v_devices->keyboard = {*keyboard_el, ::libevdev_uinput_destroy};
    devices_paths.push_back(libevdev_uinput_get_devnode(*keyboard_el));
  }

  // TODO: multiple controllers?
  libevdev_ptr controller_dev(libevdev_new(), ::libevdev_free);
  if (auto controller_el = controller::create_controller(controller_dev.get())) {
    v_devices->controllers = {{*controller_el, ::libevdev_uinput_destroy}};
    devices_paths.push_back(libevdev_uinput_get_devnode(*controller_el));
  }

  auto controller_state = std::make_shared<immer::atom<immer::box<data::CONTROLLER_MULTI_PACKET> /* prev packet */>>();
  auto keyboard_state = std::make_shared<immer::atom<immer::array<int> /* key codes */>>();

  auto ctrl_handler = event_bus->register_handler<immer::box<ControlEvent>>(
      [sess_id = session_id, v_devices, controller_state, keyboard_state](immer::box<ControlEvent> ctrl_ev) {
        if (ctrl_ev->session_id == sess_id && ctrl_ev->type == INPUT_DATA) {
          auto input = (const data::INPUT_PKT *)(ctrl_ev->raw_packet.data());

          switch ((int)boost::endian::big_to_native((int)input->type)) {
          case data::MOUSE_MOVE_REL:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_REL");
            if (v_devices->mouse) {
              mouse::move_mouse(v_devices->mouse->get(), *(data::MOUSE_MOVE_REL_PACKET *)input);
            }
            break;
          case data::MOUSE_MOVE_ABS:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_MOVE_ABS");
            if (v_devices->touchpad) {
              mouse::move_touchpad(v_devices->touchpad->get(), *(data::MOUSE_MOVE_ABS_PACKET *)input);
            }
            break;
          case data::MOUSE_BUTTON:
            logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_BUTTON");
            if (v_devices->mouse) {
              mouse::mouse_press(v_devices->mouse->get(), *(data::MOUSE_BUTTON_PACKET *)input);
            }
            break;
          case data::KEYBOARD_OR_SCROLL: {
            char *sub_input_type = (char *)input + 8;
            if (sub_input_type[0] == data::KEYBOARD_OR_SCROLL) {
              logs::log(logs::trace, "[INPUT] Received input of type: MOUSE_SCROLL_PACKET");
              if (v_devices->mouse) {
                mouse::mouse_scroll(v_devices->mouse->get(), *(data::MOUSE_SCROLL_PACKET *)input);
              }
            } else {
              logs::log(logs::trace, "[INPUT] Received input of type: KEYBOARD_PACKET");
              if (v_devices->keyboard) {
                auto kb_action = keyboard::keyboard_handle(v_devices->keyboard->get(), *(data::KEYBOARD_PACKET *)input);

                // Setting up the shared keyboard_state with the currently pressed keys
                if (kb_action) {
                  if (kb_action->pressed) { // Pressed key, add it to the key_codes
                    keyboard_state->update([&kb_action](const immer::array<int> &key_codes) {
                      return key_codes.push_back(kb_action->linux_code);
                    });
                  } else { // Released key, remove it from the key_codes
                    keyboard_state->update([&kb_action](const immer::array<int> &key_codes) {
                      return key_codes                                        //
                             | ranges::views::filter([&kb_action](int code) { //
                                 return code != kb_action->linux_code;        //
                               })                                             //
                             | ranges::to<immer::array<int>>();               //
                    });
                  }
                }
              }
            }
            break;
          }
          case data::CONTROLLER_MULTI: {
            /**
             * TODO: rumble?
             */
            logs::log(logs::trace, "[INPUT] Received input of type: CONTROLLER_MULTI");
            auto new_controller_pkt = (data::CONTROLLER_MULTI_PACKET *)input;
            auto prev_pkt = controller_state->exchange(immer::box<data::CONTROLLER_MULTI_PACKET>{*new_controller_pkt});
            if (new_controller_pkt->controller_number < v_devices->controllers.size()) {
              controller::controller_handle(v_devices->controllers[new_controller_pkt->controller_number].get(),
                                            *new_controller_pkt,
                                            prev_pkt->get());
            } else {
              logs::log(logs::warning, "[INPUT] Unable to find controller {}", new_controller_pkt->controller_number);
            }
            break;
          }
          case data::CONTROLLER:
            logs::log(logs::warning, "[INPUT] Received input of type: CONTROLLER ignoring...");
            break;
          }
        }
      });

  /**
   * We have to keep sending the EV_KEY with a value of 2 until the user release the key.
   * This needs to be done with some kind of recurring event that will be triggered
   * every 50 millis.
   *
   * Unfortunately, this event is not being sent by Moonlight.
   */
  auto kb_thread_over = std::make_shared<immer::atom<bool>>(false);
  auto keyboard_thread = std::make_shared<std::thread>([v_devices, keyboard_state, kb_thread_over]() {
    while (!kb_thread_over->load()) {
      std::this_thread::sleep_for(50ms); // TODO: should this be configurable?
      keyboard::keyboard_repeat_press(v_devices->keyboard->get(), keyboard_state->load());
    }
  });

  auto end_handler = event_bus->register_handler<immer::box<moonlight::control::TerminateEvent>>(
      [sess_id = session_id, kb_thread_over, keyboard_thread](immer::box<moonlight::control::TerminateEvent> event) {
        if (event->session_id == sess_id) {
          kb_thread_over->update([](bool terminate) { return true; });
          keyboard_thread->join();
        }
      });

  return InputReady{.devices_paths = devices_paths.persistent(),
                    .registered_handlers = immer::array<immer::box<dp::handler_registration>>{std::move(ctrl_handler),
                                                                                              std::move(end_handler)}};
}
} // namespace input