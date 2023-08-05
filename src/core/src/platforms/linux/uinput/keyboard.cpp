#include "keyboard.hpp"
#include "uinput.hpp"
#include <algorithm>
#include <core/input.hpp>
#include <helpers/logger.hpp>
#include <thread>

namespace wolf::core::input {

using namespace std::string_literals;

struct KeyboardState {
  std::thread repeat_press_t;
  bool stop_repeat_thread = false;
  libevdev_uinput_ptr kb = nullptr;
  std::vector<short> cur_press_keys = {};
};

std::vector<std::string> Keyboard::get_nodes() const {
  std::vector<std::string> nodes;

  if (auto kb = _state->kb.get()) {
    nodes.emplace_back(libevdev_uinput_get_devnode(kb));
  }

  return nodes;
}

static std::optional<libevdev_uinput *> create_keyboard(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Keyboard");
  libevdev_set_name(dev, "Wolf keyboard virtual device");
  libevdev_set_id_vendor(dev, 0xAB00);
  libevdev_set_id_product(dev, 0xAB03);
  libevdev_set_id_version(dev, 0xAB00);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, KEY_BACKSPACE, nullptr);

  for (auto ev : keyboard::key_mappings) {
    libevdev_enable_event_code(dev, EV_KEY, ev.second.linux_code, nullptr);
  }

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual keyboard {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

static std::optional<keyboard::KEY_MAP> press_btn(libevdev_uinput *kb, short key_code) {
  auto search_key = keyboard::key_mappings.find(key_code);
  if (search_key == keyboard::key_mappings.end()) {
    logs::log(logs::warning, "[INPUT] Keyboard, unrecognised key code: {}", key_code);
  } else {
    auto mapped_key = search_key->second;

    libevdev_uinput_write_event(kb, EV_MSC, MSC_SCAN, mapped_key.scan_code);
    libevdev_uinput_write_event(kb, EV_KEY, mapped_key.linux_code, 1);
    libevdev_uinput_write_event(kb, EV_SYN, SYN_REPORT, 0);
    return mapped_key;
  }
  return {};
}

Keyboard::Keyboard(std::chrono::milliseconds timeout_repress_key) {
  this->_state = std::make_shared<KeyboardState>(KeyboardState{});
  auto repeat_thread = std::thread([state = this->_state, timeout_repress_key]() {
    while (!state->stop_repeat_thread) {
      std::this_thread::sleep_for(timeout_repress_key);
      for (auto key : state->cur_press_keys) {
        if (auto keyboard = state->kb.get()) {
          press_btn(keyboard, key);
        }
      }
    }
  });
  this->_state->repeat_press_t = std::move(repeat_thread);
  this->_state->repeat_press_t.detach();

  libevdev_ptr kb_dev(libevdev_new(), ::libevdev_free);
  if (auto kb_el = create_keyboard(kb_dev.get())) {
    this->_state->kb = {*kb_el, ::libevdev_uinput_destroy};
  }
}

Keyboard::~Keyboard() {
  _state->stop_repeat_thread = true;
  if (_state->repeat_press_t.joinable()) {
    _state->repeat_press_t.join();
  }
}

void Keyboard::press(short key_code) {
  if (auto keyboard = _state->kb.get()) {
    if (auto key = press_btn(keyboard, key_code)) {
      _state->cur_press_keys.push_back(key_code);
    }
  }
}

void Keyboard::release(short key_code) {
  auto search_key = keyboard::key_mappings.find(key_code);
  if (search_key == keyboard::key_mappings.end()) {
    logs::log(logs::warning, "[INPUT] Keyboard, unrecognised key code: {}", key_code);
  } else {
    if (auto keyboard = _state->kb.get()) {
      auto mapped_key = search_key->second;
      this->_state->cur_press_keys.erase(
          std::remove(this->_state->cur_press_keys.begin(), this->_state->cur_press_keys.end(), key_code),
          this->_state->cur_press_keys.end());

      libevdev_uinput_write_event(keyboard, EV_MSC, MSC_SCAN, mapped_key.scan_code);
      libevdev_uinput_write_event(keyboard, EV_KEY, mapped_key.linux_code, 0);
      libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);
    }
  }
}

static void keyboard_ev(libevdev_uinput *keyboard, int linux_code, int event_code = 1) {
  libevdev_uinput_write_event(keyboard, EV_KEY, linux_code, event_code);
  libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);
}

void Keyboard::paste_utf(const std::basic_string<char32_t> &utf32) {
  /* To HEX string */
  auto hex_unicode = to_hex(utf32);
  logs::log(logs::debug, "[INPUT] Typing U+{}", hex_unicode);

  keyboard_ev(this->_state->kb.get(), KEY_LEFTCTRL, 1);
  keyboard_ev(this->_state->kb.get(), KEY_LEFTSHIFT, 1);
  keyboard_ev(this->_state->kb.get(), KEY_U, 1);
  keyboard_ev(this->_state->kb.get(), KEY_U, 0);

  for (auto &ch : hex_unicode) {
    auto key_str = "KEY_"s + ch;
    auto keycode = libevdev_event_code_from_name(EV_KEY, key_str.c_str());
    if (keycode == -1) {
      logs::log(logs::warning, "[INPUT] Unable to find keycode for: {}", ch);
    } else {
      keyboard_ev(this->_state->kb.get(), keycode, 1);
      keyboard_ev(this->_state->kb.get(), keycode, 0);
    }
  }

  keyboard_ev(this->_state->kb.get(), KEY_LEFTSHIFT, 0);
  keyboard_ev(this->_state->kb.get(), KEY_LEFTCTRL, 0);
}

} // namespace wolf::core::input