#include "keyboard.hpp"
#include "uinput.hpp"
#include <core/input.hpp>
#include <helpers/logger.hpp>
#include <linux/input.h>
#include <linux/uinput.h>
#include <optional>

namespace wolf::core::input {

struct JoypadState {
  Joypad::CONTROLLER_TYPE type;

  libevdev_uinput_ptr joy = nullptr;
  int currently_pressed_btns = 0;

  libevdev_uinput_ptr trackpad = nullptr;

  // We have to keep around if triggers are moving so that we can properly control BTN_TR2/BTN_TL2
  bool tr_moving = false;
  bool tl_moving = false;

  /**
   * Multi touch protocol type B is stateful; see: https://docs.kernel.org/input/multi-touch-protocol.html
   * Slots are numbered starting from 0 up to <number of currently connected fingers> (max: 4)
   *
   * The way it works:
   * - first time a new finger_id arrives we'll create a new slot and call MT_TRACKING_ID = slot_number
   * - we can keep updating ABS_X and ABS_Y as long as the finger_id stays the same
   * - if we want to update a different finger we'll have to call ABS_MT_SLOT = slot_number
   * - when a finger is released we'll call ABS_MT_SLOT = slot_number && MT_TRACKING_ID = -1
   *
   * The other thing that needs to be kept in sync is the EV_KEY.
   * EX: enabling BTN_TOOL_DOUBLETAP will result in scrolling instead of moving the mouse
   */
  struct TrackpadState {
    /* The MT_SLOT we are currently updating */
    int current_slot = -1;
    /* A map of finger_id to MT_SLOT */
    std::map<int /* finger_id */, int /* MT_SLOT */> fingers;
  } trackpad_state;

  libevdev_uinput_ptr motion_sensor = nullptr;
  /* see: MSC_TIMESTAMP */
  std::chrono::time_point<std::chrono::steady_clock> motion_sensor_startup_time = std::chrono::steady_clock::now();

  bool stop_listening_events = false;
  std::thread events_thread;

  std::optional<std::function<void(int low_freq, int high_freq)>> on_rumble = std::nullopt;
  std::optional<std::function<void(int r, int g, int b)>> on_led = std::nullopt;
};

static bool TR_TL_enabled(Joypad::CONTROLLER_TYPE type) {
  return type == Joypad::CONTROLLER_TYPE::PS || type == Joypad::CONTROLLER_TYPE::NINTENDO;
}

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
    auto additional_nodes = get_child_dev_nodes(joy);
    nodes.insert(nodes.end(), additional_nodes.begin(), additional_nodes.end());
  }

  if (auto trackpad = _state->trackpad.get()) {
    auto additional_nodes = get_child_dev_nodes(trackpad);
    nodes.insert(nodes.end(), additional_nodes.begin(), additional_nodes.end());
  }

  if (auto motion_sensor = _state->motion_sensor.get()) {
    auto additional_nodes = get_child_dev_nodes(motion_sensor);
    nodes.insert(nodes.end(), additional_nodes.begin(), additional_nodes.end());
  }

  return nodes;
}

/**
 * This needs to be the same for all the virtual devices in order for SDL to match gyro with the joypad
 * see:
 * https://github.com/libsdl-org/SDL/blob/7cc3e94eb22f2ee76742bfb4c101757fcb70c4b7/src/joystick/linux/SDL_sysjoystick.c#L1446
 */
static constexpr std::string_view UNIQ_ID = "00:11:22:33:44:55";

std::vector<std::map<std::string, std::string>> Joypad::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (auto joy = _state->joy.get()) {
    // eventXY and jsX devices
    for (const auto &devnode : get_child_dev_nodes(joy)) {
      std::string syspath = libevdev_uinput_get_syspath(joy);
      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX

      auto event = gen_udev_base_event(devnode, syspath);
      event["ID_INPUT_JOYSTICK"] = "1";
      event[".INPUT_CLASS"] = "joystick";
      event["UNIQ"] = UNIQ_ID;
      events.emplace_back(event);
    }
  }

  if (auto trackpad = _state->trackpad.get()) {
    for (const auto &devnode : get_child_dev_nodes(trackpad)) {
      std::string syspath = libevdev_uinput_get_syspath(trackpad);
      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX

      auto event = gen_udev_base_event(devnode, syspath);
      event["ID_INPUT_TOUCHPAD"] = "1";
      event[".INPUT_CLASS"] = "mouse";
      event["UNIQ"] = UNIQ_ID;
      events.emplace_back(event);
    }
  }

  if (auto motion_sensor = _state->motion_sensor.get()) {
    for (const auto &devnode : get_child_dev_nodes(motion_sensor)) {
      std::string syspath = libevdev_uinput_get_syspath(motion_sensor);
      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX

      auto event = gen_udev_base_event(devnode, syspath);
      event["ID_INPUT_ACCELEROMETER"] = "1";
      event["ID_INPUT_WIDTH_MM"] = "8";
      event["ID_INPUT_HEIGHT_MM"] = "8";
      event["UNIQ"] = UNIQ_ID;
      event["IIO_SENSOR_PROXY_TYPE"] = "input-accel";
      event["SYSTEMD_WANTS"] = "iio-sensor-proxy.service";
      events.emplace_back(event);
    }
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> Joypad::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

  if (_state->joy.get()) {
    result.push_back({gen_udev_hw_db_filename(_state->joy),
                      {"E:ID_INPUT=1",
                       "E:ID_INPUT_JOYSTICK=1",
                       "E:ID_BUS=usb",
                       "G:seat",
                       "G:uaccess",
                       "Q:seat",
                       "Q:uaccess",
                       "V:1"}});
  }

  if (_state->trackpad.get()) {
    result.push_back({gen_udev_hw_db_filename(_state->trackpad),
                      {"E:ID_INPUT=1",
                       "E:ID_INPUT_TOUCHPAD=1",
                       "E:ID_BUS=usb",
                       "G:seat",
                       "G:uaccess",
                       "Q:seat",
                       "Q:uaccess",
                       "V:1"}});
  }

  if (_state->motion_sensor.get()) {
    result.push_back({gen_udev_hw_db_filename(_state->motion_sensor),
                      {"E:ID_INPUT=1",
                       "E:ID_INPUT_ACCELEROMETER=1",
                       "E:ID_BUS=usb",
                       "G:seat",
                       "G:uaccess",
                       "Q:seat",
                       "Q:uaccess",
                       "V:1"}});
  }

  return result;
}

static void set_controller_type(libevdev *dev, Joypad::CONTROLLER_TYPE type) {
  switch (type) {
  case Joypad::UNKNOWN: // Unknown defaults to XBOX
  case Joypad::XBOX:
    // Xbox one controller
    // https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c#L147
    libevdev_set_name(dev, "Wolf X-Box One (virtual) pad");
    libevdev_set_id_vendor(dev, 0x045E);
    libevdev_set_id_product(dev, 0x02EA);
    libevdev_set_id_version(dev, 0x0408);
    break;
  case Joypad::PS:
    // Sony PS5 controller
    // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L1182
    libevdev_set_name(dev, "Wolf PS5 (virtual) pad");
    libevdev_set_id_vendor(dev, 0x054c);
    libevdev_set_id_product(dev, 0x0ce6);
    libevdev_set_id_version(dev, 0x8111);
    break;
  case Joypad::NINTENDO:
    // Nintendo switch pro controller
    // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L981
    libevdev_set_name(dev, "Wolf Nintendo (virtual) pad");
    libevdev_set_id_vendor(dev, 0x057e);
    libevdev_set_id_product(dev, 0x2009);
    libevdev_set_id_version(dev, 0x8111);
    break;
  }
}

std::optional<libevdev_uinput *> create_controller(Joypad::CONTROLLER_TYPE type, uint8_t capabilities) {
  libevdev *dev = libevdev_new();
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, UNIQ_ID.data());
  set_controller_type(dev, type);
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
  if (TR_TL_enabled(type)) {
    libevdev_enable_event_code(dev, EV_KEY, BTN_TR2, nullptr);
    libevdev_enable_event_code(dev, EV_KEY, BTN_TL2, nullptr);
  }
  if (type == Joypad::NINTENDO) {
    libevdev_enable_event_code(dev, EV_KEY, BTN_Z, nullptr); // Capture btn
  }
  libevdev_enable_event_code(dev, EV_KEY, BTN_SELECT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_MODE, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_START, nullptr);

  libevdev_enable_event_type(dev, EV_ABS);

  input_absinfo dpad{0, -1, 1, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_HAT0Y, &dpad);
  libevdev_enable_event_code(dev, EV_ABS, ABS_HAT0X, &dpad);

  input_absinfo stick{0, -32768, 32767, 16, 128, 0};
  if (type == Joypad::NINTENDO) { // see: https://github.com/games-on-whales/wolf/issues/56
    stick.fuzz = 250;
    stick.flat = 500;
  }
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RX, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &stick);
  libevdev_enable_event_code(dev, EV_ABS, ABS_RY, &stick);

  if (capabilities & Joypad::ANALOG_TRIGGERS && type != Joypad::NINTENDO) { // On Nintendo L2/R2 are just buttons!
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

static constexpr int TOUCH_MAX_X = 1920;
static constexpr int TOUCH_MAX_Y = 1080;

/**
 * All values in here have been taken from a real PS5 gamepad using evemu-record
 * You should read the kernel docs for this: https://docs.kernel.org/input/multi-touch-protocol.html
 */
std::optional<libevdev_uinput *> create_trackpad() {
  libevdev *dev = libevdev_new();
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, UNIQ_ID.data());
  libevdev_set_name(dev, "Wolf gamepad (virtual) touchpad");
  libevdev_set_id_version(dev, 0xAB00);

  libevdev_set_id_product(dev, 0xce6);
  libevdev_set_id_vendor(dev, 0x54c);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOUCH, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_FINGER, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_DOUBLETAP, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_TRIPLETAP, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOOL_QUADTAP, nullptr);

  libevdev_enable_event_type(dev, EV_ABS);
  input_absinfo mt_slot{0, 0, 4, 0, 0, 0}; // We only support up to 4 fingers
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_SLOT, &mt_slot);

  input_absinfo abs_x{0, 0, TOUCH_MAX_X, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &abs_x);
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_X, &abs_x);

  input_absinfo abs_y{0, 0, TOUCH_MAX_Y, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &abs_y);
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_Y, &abs_y);

  input_absinfo tracking{0, 0, 65535, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID, &tracking);

  libevdev_enable_property(dev, INPUT_PROP_POINTER);
  libevdev_enable_property(dev, INPUT_PROP_BUTTONPAD);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create joypad trackpad device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual controller touchpad {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

/**
 * see:
 * https://github.com/torvalds/linux/blob/305230142ae0637213bf6e04f6d9f10bbcb74af8/drivers/hid/hid-playstation.c#L139-L144
 */
static constexpr int DS_ACC_RES_PER_G = 8192;
static constexpr int DS_ACC_RANGE = (4 * DS_ACC_RES_PER_G);
static constexpr int DS_GYRO_RES_PER_DEG_S = 1024;
static constexpr int DS_GYRO_RANGE = (2048 * DS_GYRO_RES_PER_DEG_S);

/**
 * All values in here have been taken from a real PS5 gamepad using evemu-record
 * See: https://www.kernel.org/doc/html/latest/input/event-codes.html#input-prop-accelerometer
 */
std::optional<libevdev_uinput *> create_motion_sensors() {
  libevdev *dev = libevdev_new();
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, UNIQ_ID.data());
  libevdev_set_name(dev, "Wolf gamepad (virtual) motion sensors");
  libevdev_set_id_product(dev, 0xce6);
  libevdev_set_id_vendor(dev, 0x54c);
  libevdev_set_id_version(dev, 0x8111);
  libevdev_set_id_bustype(dev, BUS_USB);

  /**
   * This enables the device to be used as a motion sensor; from the kernel docs:
   * Directional axes on this device (absolute and/or relative x, y, z) represent accelerometer data.
   * Some devices also report gyroscope data, which devices can report through the rotational axes (absolute and/or
   * relative rx, ry, rz).
   */
  libevdev_enable_property(dev, INPUT_PROP_ACCELEROMETER);

  libevdev_enable_event_type(dev, EV_ABS);

  constexpr int FUZZ = 16;

  // Acceleration
  input_absinfo acc_abs_x{38, -DS_ACC_RANGE, DS_ACC_RANGE, FUZZ, 0, DS_ACC_RES_PER_G};
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &acc_abs_x);

  input_absinfo acc_abs_y{8209, -DS_ACC_RANGE, DS_ACC_RANGE, FUZZ, 0, DS_ACC_RES_PER_G};
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &acc_abs_y);

  input_absinfo acc_abs_z{1025, -DS_ACC_RANGE, DS_ACC_RANGE, FUZZ, 0, DS_ACC_RES_PER_G};
  libevdev_enable_event_code(dev, EV_ABS, ABS_Z, &acc_abs_z);

  // Gyro
  input_absinfo gyro_abs_x{-186, -DS_GYRO_RANGE, DS_GYRO_RANGE, FUZZ, 0, DS_GYRO_RES_PER_DEG_S};
  libevdev_enable_event_code(dev, EV_ABS, ABS_RX, &gyro_abs_x);

  input_absinfo gyro_abs_y{-124, -DS_GYRO_RANGE, DS_GYRO_RANGE, FUZZ, 0, DS_GYRO_RES_PER_DEG_S};
  libevdev_enable_event_code(dev, EV_ABS, ABS_RY, &gyro_abs_y);

  input_absinfo gyro_abs_z{0, -DS_GYRO_RANGE, DS_GYRO_RANGE, FUZZ, 0, DS_GYRO_RES_PER_DEG_S};
  libevdev_enable_event_code(dev, EV_ABS, ABS_RZ, &gyro_abs_z);

  /**
   * From the kernel docs https://www.kernel.org/doc/html/latest/input/event-codes.html#ev-msc
   * Used to report the number of microseconds since the last reset.
   * This event should be coded as an uint32 value, which is allowed to wrap around with no special consequence.
   * It is assumed that the time difference between two consecutive events is reliable on a reasonable time scale
   * (hours). A reset to zero can happen, in which case the time since the last event is unknown. If the device does not
   * provide this information, the driver must not provide it to user space.
   */
  libevdev_enable_event_type(dev, EV_MSC);
  libevdev_enable_event_code(dev, EV_MSC, MSC_TIMESTAMP, nullptr);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create joypad motion sensor device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual controller motion sensor {}", libevdev_uinput_get_devnode(uidev));

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
  // All duration values are expressed in ms. Values above 32767 ms (0x7fff) should not be used
  auto delay = std::chrono::milliseconds{std::clamp(effect.replay.delay, (__u16)0, (__u16)32767)};
  auto length = std::chrono::milliseconds{std::clamp(effect.replay.length, (__u16)0, (__u16)32767)};
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
    std::this_thread::sleep_for(20ms); // TODO: configurable?

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

Joypad::Joypad(Joypad::CONTROLLER_TYPE type, uint8_t capabilities) {
  this->_state = std::make_shared<JoypadState>(JoypadState{.type = type});

  if (auto joy_el = create_controller(type, capabilities)) {
    this->_state->joy = {*joy_el, ::libevdev_uinput_destroy};

    auto event_thread = std::thread(event_listener, this->_state);
    this->_state->events_thread = std::move(event_thread);
    this->_state->events_thread.detach();
  }

  if (capabilities & Joypad::TOUCHPAD) {
    if (auto trackpad = create_trackpad()) {
      this->_state->trackpad = {*trackpad, ::libevdev_uinput_destroy};
    }
  }

  if (capabilities & Joypad::GYRO || capabilities & Joypad::ACCELEROMETER) {
    if (auto ms = create_motion_sensors()) {
      this->_state->motion_sensor = {*ms, ::libevdev_uinput_destroy};
    }
  }
}

Joypad::~Joypad() {
  _state->stop_listening_events = true;
  if (_state->joy.get() != nullptr && _state->events_thread.joinable()) {
    _state->events_thread.join();
  }
}

void Joypad::set_pressed_buttons(int newly_pressed) {
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
      if (MISC_FLAG & bf_changed && this->_state->type == NINTENDO) {
        // Capture button
        libevdev_uinput_write_event(controller, EV_KEY, BTN_Z, bf_new & MISC_FLAG ? 1 : 0);
      }
      if (A & bf_changed)
        libevdev_uinput_write_event(controller,
                                    EV_KEY,
                                    this->_state->type == NINTENDO ? BTN_EAST : BTN_SOUTH,
                                    bf_new & A ? 1 : 0);
      if (B & bf_changed)
        libevdev_uinput_write_event(controller,
                                    EV_KEY,
                                    this->_state->type == NINTENDO ? BTN_SOUTH : BTN_EAST,
                                    bf_new & B ? 1 : 0);
      if (X & bf_changed) {
        auto btn_code = this->_state->type == PS ? BTN_WEST : BTN_NORTH;
        libevdev_uinput_write_event(controller, EV_KEY, btn_code, bf_new & X ? 1 : 0);
      }
      if (Y & bf_changed) {
        auto btn_code = this->_state->type == PS ? BTN_NORTH : BTN_WEST;
        libevdev_uinput_write_event(controller, EV_KEY, btn_code, bf_new & Y ? 1 : 0);
      }

      if (TOUCHPAD_FLAG & bf_changed) {
        if (auto touchpad = this->_state->trackpad.get()) {
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_LEFT, bf_new & TOUCHPAD_FLAG ? 1 : 0);
          libevdev_uinput_write_event(touchpad, EV_SYN, SYN_REPORT, 0);
        }
      }
    }

    libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
  }
  this->_state->currently_pressed_btns = bf_new;
}

void Joypad::set_stick(Joypad::STICK_POSITION stick_type, short x, short y) {
  if (auto controller = this->_state->joy.get()) {
    if (stick_type == LS) {
      libevdev_uinput_write_event(controller, EV_ABS, ABS_X, x);
      libevdev_uinput_write_event(controller, EV_ABS, ABS_Y, -y);
    } else {
      libevdev_uinput_write_event(controller, EV_ABS, ABS_RX, x);
      libevdev_uinput_write_event(controller, EV_ABS, ABS_RY, -y);
    }

    libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
  }
}

void Joypad::set_triggers(int16_t left, int16_t right) {
  if (auto controller = this->_state->joy.get()) {
    if (this->_state->type == NINTENDO) {
      // Nintendo ZL and ZR are just buttons (EV_KEY)
      libevdev_uinput_write_event(controller, EV_KEY, BTN_TL2, left > 0 ? 1 : 0);
      libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);

      libevdev_uinput_write_event(controller, EV_KEY, BTN_TR2, right > 0 ? 1 : 0);
      libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
    } else {
      if (left > 0) {
        if (!this->_state->tl_moving && TR_TL_enabled(this->_state->type)) { // first time moving left trigger
          libevdev_uinput_write_event(controller, EV_ABS, BTN_TL2, 1);
          this->_state->tl_moving = true;
        }
        libevdev_uinput_write_event(controller, EV_ABS, ABS_Z, left);
      } else {
        if (this->_state->tl_moving && TR_TL_enabled(this->_state->type)) { // returning to the idle position
          libevdev_uinput_write_event(controller, EV_ABS, BTN_TL2, 0);
          this->_state->tl_moving = false;
        }
        libevdev_uinput_write_event(controller, EV_ABS, ABS_Z, left);
      }

      if (right > 0) {
        if (!this->_state->tr_moving && TR_TL_enabled(this->_state->type)) { // first time moving right trigger
          libevdev_uinput_write_event(controller, EV_ABS, BTN_TR2, 1);
          this->_state->tr_moving = true;
        }
        libevdev_uinput_write_event(controller, EV_ABS, ABS_RZ, right);
      } else {
        if (this->_state->tr_moving && TR_TL_enabled(this->_state->type)) { // returning to the idle position
          libevdev_uinput_write_event(controller, EV_ABS, BTN_TR2, 0);
          this->_state->tr_moving = false;
        }
        libevdev_uinput_write_event(controller, EV_ABS, ABS_RZ, right);
      }

      libevdev_uinput_write_event(controller, EV_SYN, SYN_REPORT, 0);
    }
  }
}

void Joypad::set_on_rumble(const std::function<void(int, int)> &callback) {
  this->_state->on_rumble = callback;
}

void Joypad::set_motion(MOTION_TYPE type, float x, float y, float z) {
  if (auto motion_sensor = this->_state->motion_sensor.get()) {
    switch (type) {
    case GYROSCOPE: {
      auto x_clamped = std::clamp((int)x, -DS_GYRO_RANGE, DS_GYRO_RANGE);
      auto y_clamped = std::clamp((int)y, -DS_GYRO_RANGE, DS_GYRO_RANGE);
      auto z_clamped = std::clamp((int)z, -DS_GYRO_RANGE, DS_GYRO_RANGE);

      libevdev_uinput_write_event(motion_sensor, EV_ABS, ABS_RX, x_clamped);
      libevdev_uinput_write_event(motion_sensor, EV_ABS, ABS_RY, y_clamped);
      libevdev_uinput_write_event(motion_sensor, EV_ABS, ABS_RZ, z_clamped);
      break;
    }
    case ACCELERATION: {
      auto x_clamped = std::clamp((int)x, -DS_ACC_RANGE, DS_ACC_RANGE);
      auto y_clamped = std::clamp((int)y, -DS_ACC_RANGE, DS_ACC_RANGE);
      auto z_clamped = std::clamp((int)z, -DS_ACC_RANGE, DS_ACC_RANGE);

      libevdev_uinput_write_event(motion_sensor, EV_ABS, ABS_X, x_clamped);
      libevdev_uinput_write_event(motion_sensor, EV_ABS, ABS_Y, y_clamped);
      libevdev_uinput_write_event(motion_sensor, EV_ABS, ABS_Z, z_clamped);
      break;
    }
    }

    auto time_since_last_reset = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - this->_state->motion_sensor_startup_time);
    libevdev_uinput_write_event(motion_sensor, EV_MSC, MSC_TIMESTAMP, time_since_last_reset.count());

    libevdev_uinput_write_event(motion_sensor, EV_SYN, SYN_REPORT, 0);
  }
};

void Joypad::set_battery(BATTERY_STATE state, int percentage){

};

void Joypad::set_on_led(const std::function<void(int r, int g, int b)> &callback) {
  this->_state->on_led = callback;
}

void Joypad::touchpad_place_finger(int finger_nr, float x, float y) {
  if (auto touchpad = this->_state->trackpad.get()) {
    int scaled_x = (int)std::lround(TOUCH_MAX_X * x);
    int scaled_y = (int)std::lround(TOUCH_MAX_Y * y);

    if (_state->trackpad_state.fingers.find(finger_nr) == _state->trackpad_state.fingers.end()) {
      // Wow, a wild finger appeared!
      auto finger_slot = _state->trackpad_state.fingers.size();
      _state->trackpad_state.fingers[finger_nr] = finger_slot;
      libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_SLOT, finger_slot);
      libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_TRACKING_ID, finger_slot);

      { // Update number of fingers pressed
        if (_state->trackpad_state.fingers.size() == 1) {
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_FINGER, 1);
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOUCH, 1);
        } else if (_state->trackpad_state.fingers.size() == 2) {
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_FINGER, 0);
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
        } else if (_state->trackpad_state.fingers.size() == 3) {
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
        } else if (_state->trackpad_state.fingers.size() == 4) {
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
          libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_QUADTAP, 1);
        } else {
          logs::log(logs::warning, "Joypad, exceeded max number of fingers of 4");
        }
      }
    } else {
      // I already know this finger, let's check the slot
      auto finger_slot = _state->trackpad_state.fingers[finger_nr];
      if (_state->trackpad_state.current_slot != finger_slot) {
        libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_SLOT, finger_slot);
        _state->trackpad_state.current_slot = finger_slot;
      }
    }

    libevdev_uinput_write_event(touchpad, EV_ABS, ABS_X, scaled_x);
    libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_POSITION_X, scaled_x);
    libevdev_uinput_write_event(touchpad, EV_ABS, ABS_Y, scaled_y);
    libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_POSITION_Y, scaled_y);

    libevdev_uinput_write_event(touchpad, EV_SYN, SYN_REPORT, 0);
  }
}

void Joypad::touchpad_release_finger(int finger_nr) {
  if (auto touchpad = this->_state->trackpad.get()) {
    auto finger_slot = _state->trackpad_state.fingers[finger_nr];
    if (_state->trackpad_state.current_slot != finger_slot) {
      libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_SLOT, finger_slot);
      _state->trackpad_state.current_slot = -1;
    }

    _state->trackpad_state.fingers.erase(finger_nr);
    libevdev_uinput_write_event(touchpad, EV_ABS, ABS_MT_TRACKING_ID, -1);

    { // Update number of fingers pressed
      if (_state->trackpad_state.fingers.size() == 0) {
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_FINGER, 0);
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOUCH, 0);
      } else if (_state->trackpad_state.fingers.size() == 1) {
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_FINGER, 1);
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
      } else if (_state->trackpad_state.fingers.size() == 2) {
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
      } else if (_state->trackpad_state.fingers.size() == 3) {
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
        libevdev_uinput_write_event(touchpad, EV_KEY, BTN_TOOL_QUADTAP, 0);
      } else {
        logs::log(logs::warning, "Joypad, exceeded max number of fingers of 4");
      }
    }

    libevdev_uinput_write_event(touchpad, EV_SYN, SYN_REPORT, 0);
  }
}

} // namespace wolf::core::input