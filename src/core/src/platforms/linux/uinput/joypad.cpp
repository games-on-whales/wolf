#include "keyboard.hpp"
#include "uinput.hpp"
#include <core/input.hpp>
#include <helpers/logger.hpp>

namespace wolf::core::input {

struct JoypadState {
  libevdev_uinput_ptr joy = nullptr;
  short currently_pressed_btns = 0;
};

/**
 * Joypads will also have one `/dev/input/js*` device as child, we want to expose that as well
 */
std::vector<std::string> get_child_dev_nodes(libevdev_uinput *device) {
  std::vector<std::string> result;
  auto udev = udev_new();
  if (auto device_ptr = udev_device_new_from_syspath(udev, libevdev_uinput_get_syspath(device))) {
    auto enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_parent(enumerate, device_ptr);
    udev_enumerate_scan_devices(enumerate);

    udev_list_entry *dev_list_entry;
    auto devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry_foreach(dev_list_entry, devices) {
      auto path = udev_list_entry_get_name(dev_list_entry);
      auto child_dev = udev_device_new_from_syspath(udev, path);
      if (auto dev_path = udev_device_get_devnode(child_dev)) {
        result.push_back(dev_path);
        logs::log(logs::debug, "[INPUT] Found child: {} - {}", path, dev_path);
      }
      udev_device_unref(child_dev);
    }

    udev_enumerate_unref(enumerate);
    udev_device_unref(device_ptr);
  }

  udev_unref(udev);
  return result;
}

std::vector<std::string> Joypad::get_nodes() const {
  std::vector<std::string> nodes;

  if (auto joy = _state->joy.get()) {
    auto dev_node = libevdev_uinput_get_devnode(joy);
    nodes.emplace_back(dev_node);

    auto additional_nodes = get_child_dev_nodes(joy);
    nodes.insert(nodes.end(), additional_nodes.begin(), additional_nodes.end());
  }

  return nodes;
}

static void set_controller_type(libevdev *dev, Joypad::CONTROLLER_TYPE type) {
  switch (type) {
  case Joypad::UNKNOWN: // Unknown defaults to XBOX
  case Joypad::XBOX:
    // Xbox one controller
    // https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c#L147
    libevdev_set_name(dev, "Wolf X-Box One (virtual) pad");
    libevdev_set_id_vendor(dev, 0x045E);
    libevdev_set_id_product(dev, 0x02D1);
    break;
  case Joypad::PS:
    // Sony PS5 controller
    // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L1182
    libevdev_set_name(dev, "Wolf PS5 (virtual) pad");
    libevdev_set_id_vendor(dev, 0x054c);
    libevdev_set_id_product(dev, 0x0ce6);
    break;
  case Joypad::NINTENDO:
    // Nintendo switch pro controller
    // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L981
    libevdev_set_name(dev, "Wolf Nintendo (virtual) pad");
    libevdev_set_id_vendor(dev, 0x057e);
    libevdev_set_id_product(dev, 0x2009);
    break;
  }
}

std::optional<libevdev_uinput *> create_controller(libevdev *dev, Joypad::CONTROLLER_TYPE type, uint8_t capabilities) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf gamepad");
  set_controller_type(dev, type);
  libevdev_set_id_version(dev, 0xAB00);
  libevdev_set_id_bustype(dev, BUS_USB);

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

  libevdev_enable_event_type(dev, EV_ABS);

  input_absinfo dpad{0, -1, 1, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_HAT0Y, &dpad);
  libevdev_enable_event_code(dev, EV_ABS, ABS_HAT0X, &dpad);

  input_absinfo stick{0, -32768, 32767, 16, 128, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RX, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RY, &stick);

  if (capabilities & Joypad::ANALOG_TRIGGERS) {
    input_absinfo trigger{0, 0, 255, 0, 0, 0};
    libevdev_enable_event_code(dev, EV_ABS, ABS_Z, &trigger);
    libevdev_enable_event_code(dev, EV_ABS, ABS_RZ, &trigger);
  }

  if (capabilities & Joypad::RUMBLE) {
    libevdev_enable_event_type(dev, EV_FF);
    libevdev_enable_event_code(dev, EV_FF, FF_RUMBLE, nullptr);
    libevdev_enable_event_code(dev, EV_FF, FF_CONSTANT, nullptr);
    libevdev_enable_event_code(dev, EV_FF, FF_PERIODIC, nullptr);
    libevdev_enable_event_code(dev, EV_FF, FF_SINE, nullptr);
    libevdev_enable_event_code(dev, EV_FF, FF_RAMP, nullptr);
    libevdev_enable_event_code(dev, EV_FF, FF_GAIN, nullptr);
  }

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create controller device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual controller {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

Joypad::Joypad(Joypad::CONTROLLER_TYPE type, uint8_t capabilities) {
  this->_state = std::make_shared<JoypadState>();

  libevdev_ptr joy_dev(libevdev_new(), ::libevdev_free);
  if (auto joy_el = create_controller(joy_dev.get(), type, capabilities)) {
    this->_state->joy = {*joy_el, ::libevdev_uinput_destroy};
  }
}

void Joypad::set_pressed_buttons(short newly_pressed) {
  // Button flags that have been changed between current and prev
  auto bf_changed = newly_pressed ^ this->_state->currently_pressed_btns;
  // Button flags that are only part of the new packet
  auto bf_new = newly_pressed;
  if (auto controller = this->_state->joy.get()) {

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

    libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
  }
  this->_state->currently_pressed_btns = bf_new;
}

void Joypad::set_stick(Joypad::STICK_POSITION stick_type, short x, short y) {
  if (auto controller = this->_state->joy.get()) {
    if (stick_type == L2) {
      libevdev_uinput_write_event(controller, EV_ABS, ABS_X, x);
      libevdev_uinput_write_event(controller, EV_ABS, ABS_Y, -y);
    } else {
      libevdev_uinput_write_event(controller, EV_ABS, ABS_RX, x);
      libevdev_uinput_write_event(controller, EV_ABS, ABS_RY, -y);
    }

    libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
  }
}

void Joypad::set_triggers(unsigned char left, unsigned char right) {
  if (auto controller = this->_state->joy.get()) {
    libevdev_uinput_write_event(controller, EV_ABS, ABS_Z, left);
    libevdev_uinput_write_event(controller, EV_ABS, ABS_RZ, right);

    libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
  }
}

void Joypad::set_on_rumble(const std::function<void(int)> &callback) {}

void Joypad::set_on_motion(const std::function<void(MOTION_TYPE, int, int, int)> &callback){};

void Joypad::set_on_battery(const std::function<void(BATTERY_STATE, int)> &callback){};

} // namespace wolf::core::input