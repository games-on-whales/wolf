#include "keyboard.hpp"
#include "uinput.hpp"
#include <core/input.hpp>
#include <helpers/logger.hpp>
#include <linux/input.h>
#include <linux/uinput.h>
#include <optional>

namespace wolf::core::input {

struct JoypadState {
  libevdev_uinput_ptr joy = nullptr;
  short currently_pressed_btns = 0;

  bool stop_listening_events = false;
  std::thread events_thread;

  std::optional<std::function<void(int low_freq, int high_freq)>> on_rumble = std::nullopt;
  std::optional<std::function<void(Joypad::MOTION_TYPE type, int x, int y, int z)>> on_motion = std::nullopt;
  std::optional<std::function<void(Joypad::BATTERY_STATE state, int percentage)>> on_battery = std::nullopt;
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

std::optional<libevdev_uinput *> create_controller(Joypad::CONTROLLER_TYPE type, uint8_t capabilities) {
  libevdev *dev = libevdev_new();
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

struct ActiveRumbleEffect {
  int effect_id;

  std::chrono::steady_clock::time_point start_point;
  std::chrono::steady_clock::time_point end_point;
  std::chrono::milliseconds length;
  ff_envelope envelope;
  struct {
    std::uint32_t weak, strong;
  } start;

  struct {
    std::uint32_t weak, strong;
  } end;
  int gain = 1;

  std::pair<std::uint32_t, std::uint32_t> previous = {0, 0};
};

static std::uint32_t rumble_magnitude(std::chrono::milliseconds time_left,
                                      std::uint32_t start,
                                      std::uint32_t end,
                                      std::chrono::milliseconds length) {
  auto rel = end - start;
  return start + (rel * time_left.count() / length.count());
}

static std::pair<std::uint32_t, std::uint32_t> simulate_rumble(const ActiveRumbleEffect &effect,
                                                               const std::chrono::steady_clock::time_point &now) {
  if (now < effect.start_point) {
    return {0, 0}; // needs to be delayed
  }

  auto time_left = std::chrono::duration_cast<std::chrono::milliseconds>(effect.end_point - now);
  auto t = effect.length - time_left;
  std::uint32_t weak = 0, strong = 0;

  if (t.count() < effect.envelope.attack_length) {
    weak = (effect.envelope.attack_level * t.count() + weak * (effect.envelope.attack_length - t.count())) /
           effect.envelope.attack_length;
    strong = (effect.envelope.attack_level * t.count() + strong * (effect.envelope.attack_length - t.count())) /
             effect.envelope.attack_length;
  } else if (time_left.count() < effect.envelope.fade_length) {
    auto dt = (t - effect.length).count() + effect.envelope.fade_length;

    weak = (effect.envelope.fade_level * dt + weak * (effect.envelope.fade_length - dt)) / effect.envelope.fade_length;
    strong = (effect.envelope.fade_level * dt + strong * (effect.envelope.fade_length - dt)) /
             effect.envelope.fade_length;
  } else {
    weak = rumble_magnitude(t, effect.start.weak, effect.end.weak, effect.length);
    strong = rumble_magnitude(t, effect.start.strong, effect.end.strong, effect.length);
  }

  weak = weak * effect.gain;
  strong = strong * effect.gain;
  return {weak, strong};
}

static ActiveRumbleEffect create_rumble_effect(int effect_id, int effect_gain, const ff_effect &effect) {
  auto delay = std::chrono::milliseconds{effect.replay.delay};
  auto length = std::chrono::milliseconds{effect.replay.length};
  auto now = std::chrono::steady_clock::now();
  ActiveRumbleEffect r_effect{.effect_id = effect_id,
                              .start_point = now + delay,
                              .end_point = now + delay + length,
                              .length = length,
                              .envelope = {},
                              .gain = effect_gain};
  switch (effect.type) {
  case FF_CONSTANT:
    r_effect.start.weak = effect.u.constant.level;
    r_effect.start.strong = effect.u.constant.level;
    r_effect.end.weak = effect.u.constant.level;
    r_effect.end.strong = effect.u.constant.level;
    r_effect.envelope = effect.u.constant.envelope;
    break;
  case FF_PERIODIC:
    r_effect.start.weak = effect.u.periodic.magnitude;
    r_effect.start.strong = effect.u.periodic.magnitude;
    r_effect.end.weak = effect.u.periodic.magnitude;
    r_effect.end.strong = effect.u.periodic.magnitude;
    r_effect.envelope = effect.u.periodic.envelope;
    break;
  case FF_RAMP:
    r_effect.start.weak = effect.u.ramp.start_level;
    r_effect.start.strong = effect.u.ramp.start_level;
    r_effect.end.weak = effect.u.ramp.end_level;
    r_effect.end.strong = effect.u.ramp.end_level;
    r_effect.envelope = effect.u.ramp.envelope;
    break;
  case FF_RUMBLE:
    r_effect.start.weak = effect.u.rumble.weak_magnitude;
    r_effect.start.strong = effect.u.rumble.strong_magnitude;
    r_effect.end.weak = effect.u.rumble.weak_magnitude;
    r_effect.end.strong = effect.u.rumble.strong_magnitude;
    break;
  }
  return r_effect;
}

/**
 * Here we listen for events from the device and call the corresponding callback functions
 *
 * Rumble:
 *   First of, this is called force feedback (FF) in linux,
 *   you can see some docs here: https://www.kernel.org/doc/html/latest/input/ff.html
 *   In uinput this works as a two step process:
 *    - you first upload the FF effect with a given request ID
 *    - later on when the rumble has been activated you'll receive an EV_FF in your /dev/input/event**
 *      where the value is the request ID
 *   You can test the virtual devices that we create by simply using the utility `fftest`
 */
static void event_listener(const std::shared_ptr<JoypadState> &state) {
  std::this_thread::sleep_for(100ms); // We have to sleep in order to be able to read from the newly created device

  auto uinput_fd = libevdev_uinput_get_fd(state->joy.get());
  if (uinput_fd < 0) {
    logs::log(logs::warning, "Unable to open uinput device, additional events will be disabled.");
    return;
  }

  // We have to add 0_NONBLOCK to the flags in order to be able to read the events
  int flags = fcntl(uinput_fd, F_GETFL, 0);
  fcntl(uinput_fd, F_SETFL, flags | O_NONBLOCK);

  /* Local copy of all the uploaded ff effects */
  std::map<int, ff_effect> ff_effects = {};

  /* Currently running ff effects */
  std::vector<ActiveRumbleEffect> active_effects = {};

  auto remove_effects = [&](auto filter_fn) {
    active_effects.erase(std::remove_if(active_effects.begin(),
                                        active_effects.end(),
                                        [&](const auto effect) {
                                          auto to_be_removed = filter_fn(effect);
                                          if (to_be_removed && state->on_rumble) {
                                            state->on_rumble.value()(0, 0);
                                          }
                                          return to_be_removed;
                                        }),
                         active_effects.end());
  };

  while (!state->stop_listening_events) {
    std::this_thread::sleep_for(50ms); // TODO: configurable?

    int effect_gain = 1;

    auto events = fetch_events(uinput_fd);
    for (auto ev : events) {
      if (ev->type == EV_UINPUT && ev->code == UI_FF_UPLOAD) { // Upload a new FF effect
        uinput_ff_upload upload{};
        upload.request_id = ev->value;

        ioctl(uinput_fd, UI_BEGIN_FF_UPLOAD, &upload); // retrieve the effect

        logs::log(logs::debug, "Joypad, received FF upload request, effect_id: {}", upload.effect.id);
        ff_effects.insert_or_assign(upload.effect.id, upload.effect);
        upload.retval = 0;

        ioctl(uinput_fd, UI_END_FF_UPLOAD, &upload);
      } else if (ev->type == EV_UINPUT && ev->code == UI_FF_ERASE) { // Remove an uploaded FF effect
        uinput_ff_erase erase{};
        erase.request_id = ev->value;

        ioctl(uinput_fd, UI_BEGIN_FF_ERASE, &erase); // retrieve ff_erase

        logs::log(logs::debug, "Joypad, received FF erase request, effect_id: {}", erase.effect_id);
        ff_effects.erase(erase.effect_id);
        erase.retval = 0;

        ioctl(uinput_fd, UI_END_FF_ERASE, &erase);
      } else if (ev->type == EV_FF && ev->code == FF_GAIN) { // Force feedback set gain
        logs::log(logs::debug, "Joypad, received FF gain request, gain: {}", ev->value);
        effect_gain = std::clamp(ev->value, 0, 0xFFFF);
      } else if (ev->type == EV_FF) { // Force feedback effect
        auto effect_id = ev->code;
        if (ev->value) { // Activate
          logs::log(logs::debug, "Joypad, starting rumble effect: {}", effect_id);
          if (ff_effects.find(effect_id) != ff_effects.end() && state->on_rumble) {
            auto effect = ff_effects[effect_id];
            active_effects.emplace_back(create_rumble_effect(effect_id, effect_gain, effect));
          } else {
            logs::log(logs::warning, "Unknown rumble effect: {}", effect_id);
          }
        } else { // Deactivate
          logs::log(logs::debug, "Joypad, ending rumble effect: {}", effect_id);
          remove_effects([effect_id](const auto &effect) { return effect.effect_id == effect_id; });
        }
      } else if (ev->type == EV_LED) {
        logs::log(logs::debug, "Joypad, received EV_LED: {}", ev->value);
        // TODO: support LED
      }
    }

    auto now = std::chrono::steady_clock::now();

    // Remove effects that have ended
    remove_effects([now](const auto effect) { return effect.end_point <= now; });

    // Simulate rumble
    for (auto effect : active_effects) {
      auto [weak, strong] = simulate_rumble(effect, now);
      if (effect.previous.first != weak || effect.previous.second != strong) {
        effect.previous.first = weak;
        effect.previous.second = strong;

        if (auto callback = state->on_rumble) {
          callback.value()(weak, strong);
        }
      }
    }
  }
}

static void destroy_controller(JoypadState *state) {
  state->stop_listening_events = true;
  if (state->joy.get() != nullptr) {
    state->events_thread.join();
  }
  free(state);
}

Joypad::Joypad(Joypad::CONTROLLER_TYPE type, uint8_t capabilities) {
  auto state = new JoypadState;
  this->_state = std::shared_ptr<JoypadState>(state, destroy_controller);

  if (auto joy_el = create_controller(type, capabilities)) {
    this->_state->joy = {*joy_el, ::libevdev_uinput_destroy};

    auto event_thread = std::thread(event_listener, this->_state);
    this->_state->events_thread = std::move(event_thread);
    this->_state->events_thread.detach();
  }
}

Joypad::~Joypad() {}

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

void Joypad::set_on_rumble(const std::function<void(int, int)> &callback) {
  this->_state->on_rumble = callback;
}

void Joypad::set_on_motion(const std::function<void(MOTION_TYPE, int, int, int)> &callback) {
  this->_state->on_motion = callback;
};

void Joypad::set_on_battery(const std::function<void(BATTERY_STATE, int)> &callback) {
  this->_state->on_battery = callback;
};

} // namespace wolf::core::input