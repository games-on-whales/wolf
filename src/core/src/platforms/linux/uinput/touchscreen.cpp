#include "uinput.hpp"
#include <core/input.hpp>
#include <linux/input.h>

namespace wolf::core::input {

struct TouchScreenState {
  libevdev_uinput_ptr touch_screen = nullptr;

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
  /* The MT_SLOT we are currently updating */
  int current_slot = -1;
  /* A map of finger_id to MT_SLOT */
  std::map<int /* finger_id */, int /* MT_SLOT */> fingers;
};

std::vector<std::string> TouchScreen::get_nodes() const {
  std::vector<std::string> nodes;

  if (auto kb = _state->touch_screen.get()) {
    nodes.emplace_back(libevdev_uinput_get_devnode(kb));
  }

  return nodes;
}

std::vector<std::map<std::string, std::string>> TouchScreen::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (_state->touch_screen.get()) {
    auto event = gen_udev_base_event(_state->touch_screen);
    event["ID_INPUT_TOUCHSCREEN"] = "1";
    events.emplace_back(event);
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> TouchScreen::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

  if (_state->touch_screen.get()) {
    result.push_back({gen_udev_hw_db_filename(_state->touch_screen),
                      {"E:ID_INPUT=1",
                       "E:ID_INPUT_TOUCHSCREEN=1",
                       "E:ID_BUS=usb",
                       "G:seat",
                       "G:uaccess",
                       "Q:seat",
                       "Q:uaccess",
                       "V:1"}});
  }

  return result;
}

static constexpr int TOUCH_MAX_X = 19200;
static constexpr int TOUCH_MAX_Y = 10800;
static constexpr int NUM_FINGERS = 16;
static constexpr int PRESSURE_MAX = 253;

std::optional<libevdev_uinput *> create_touch_screen() {
  libevdev *dev = libevdev_new();
  libevdev_uinput *uidev;

  libevdev_set_name(dev, "Wolf (virtual) touch screen");
  libevdev_set_id_version(dev, 0xAB00);

  libevdev_set_id_product(dev, 0xAB01);
  libevdev_set_id_version(dev, 0xAB00);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, nullptr);
  libevdev_enable_event_code(dev, EV_KEY, BTN_TOUCH, nullptr);

  libevdev_enable_event_type(dev, EV_ABS);
  input_absinfo mt_slot{0, 0, NUM_FINGERS - 1, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_SLOT, &mt_slot);

  input_absinfo abs_x{0, 0, TOUCH_MAX_X, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &abs_x);
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_X, &abs_x);

  input_absinfo abs_y{0, 0, TOUCH_MAX_Y, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &abs_y);
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_Y, &abs_y);

  input_absinfo tracking{0, 0, 65535, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID, &tracking);

  input_absinfo abs_pressure{0, 0, PRESSURE_MAX, 0, 0, 0};
  libevdev_enable_event_code(dev, EV_ABS, ABS_PRESSURE, &abs_pressure);
  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_PRESSURE, &abs_pressure);
  // TODO:
  //  input_absinfo touch{0, 0, TOUCH_MAX, 4, 0, 0};
  //  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TOUCH_MAJOR, &touch);
  //  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TOUCH_MINOR, &touch);
  //  input_absinfo orientation{0, -3, 4, 0, 0, 0};
  //  libevdev_enable_event_code(dev, EV_ABS, ABS_MT_ORIENTATION, &orientation);

  // https://docs.kernel.org/input/event-codes.html#touchscreens
  libevdev_enable_property(dev, INPUT_PROP_DIRECT);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create touch screen device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created touch screen touchpad {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

TouchScreen::TouchScreen() {
  this->_state = std::make_shared<TouchScreenState>();
  if (auto touch_screen = create_touch_screen()) {
    this->_state->touch_screen = {*touch_screen, ::libevdev_uinput_destroy};
  }
}
TouchScreen::~TouchScreen() {}
void TouchScreen::place_finger(int finger_nr, float x, float y, float pressure, int orientation) {
  if (auto ts = this->_state->touch_screen.get()) {
    int scaled_x = (int)std::lround(TOUCH_MAX_X * x);
    int scaled_y = (int)std::lround(TOUCH_MAX_Y * y);
    int scaled_orientation = std::clamp(orientation, -90, 90);

    if (_state->fingers.find(finger_nr) == _state->fingers.end()) {
      // Wow, a wild finger appeared!
      auto finger_slot = _state->fingers.size() + 1;
      _state->fingers[finger_nr] = finger_slot;
      libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_SLOT, finger_slot);
      libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_TRACKING_ID, finger_slot);
    } else {
      // I already know this finger, let's check the slot
      auto finger_slot = _state->fingers[finger_nr];
      if (_state->current_slot != finger_slot) {
        libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_SLOT, finger_slot);
        _state->current_slot = finger_slot;
      }
    }

    libevdev_uinput_write_event(ts, EV_ABS, ABS_X, scaled_x);
    libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_POSITION_X, scaled_x);
    libevdev_uinput_write_event(ts, EV_ABS, ABS_Y, scaled_y);
    libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_POSITION_Y, scaled_y);
    libevdev_uinput_write_event(ts, EV_ABS, ABS_PRESSURE, (int) std::lround(pressure * PRESSURE_MAX));
    libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_PRESSURE, (int) std::lround(pressure * PRESSURE_MAX));
    libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_ORIENTATION, scaled_orientation);

    libevdev_uinput_write_event(ts, EV_SYN, SYN_REPORT, 0);
  }
}

void TouchScreen::release_finger(int finger_nr) {
  if (auto ts = this->_state->touch_screen.get()) {
    auto finger_slot = _state->fingers[finger_nr];
    if (_state->current_slot != finger_slot) {
      libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_SLOT, finger_slot);
      _state->current_slot = -1;
    }
    _state->fingers.erase(finger_nr);
    libevdev_uinput_write_event(ts, EV_ABS, ABS_MT_TRACKING_ID, -1);

    libevdev_uinput_write_event(ts, EV_SYN, SYN_REPORT, 0);
  }
}

} // namespace wolf::core::input