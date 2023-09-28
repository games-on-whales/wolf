#include "uinput.hpp"
#include <core/input.hpp>
#include <helpers/logger.hpp>

namespace wolf::core::input {

struct MouseState {
  libevdev_uinput_ptr mouse_rel = nullptr;
  libevdev_uinput_ptr mouse_abs = nullptr;
};

std::vector<std::string> Mouse::get_nodes() const {
  std::vector<std::string> nodes;

  if (auto mouse = _state->mouse_rel.get()) {
    nodes.emplace_back(libevdev_uinput_get_devnode(mouse));
  }

  if (auto mouse = _state->mouse_abs.get()) {
    nodes.emplace_back(libevdev_uinput_get_devnode(mouse));
  }

  return nodes;
}

std::vector<std::map<std::string, std::string>> Mouse::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (_state->mouse_rel.get()) {
    auto base = gen_udev_base_event(_state->mouse_rel);
    base["ID_INPUT_MOUSE"] = "1";
    base[".INPUT_CLASS"] = "mouse";
    events.emplace_back(std::move(base));
  }

  if (_state->mouse_abs.get()) {
    auto base = gen_udev_base_event(_state->mouse_abs);
    base["ID_INPUT_TOUCHPAD"] = "1";
    base[".INPUT_CLASS"] = "mouse";
    events.emplace_back(std::move(base));
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> Mouse::get_udev_hw_db_entries() const {
  return {};
}

constexpr int ABS_MAX_WIDTH = 19200;
constexpr int ABS_MAX_HEIGHT = 12000;

static std::optional<libevdev_uinput *> create_mouse(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Mouse");
  libevdev_set_name(dev, "Wolf mouse virtual device");
  libevdev_set_id_vendor(dev, 0xAB00);
  libevdev_set_id_product(dev, 0xAB01);
  libevdev_set_id_version(dev, 0xAB00);
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
    logs::log(logs::error, "Unable to create mouse device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual mouse {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

static std::optional<libevdev_uinput *> create_mouse_abs(libevdev *dev) {
  libevdev_uinput *uidev;

  libevdev_set_uniq(dev, "Wolf Touchpad");
  libevdev_set_name(dev, "Wolf touchpad virtual device");
  libevdev_set_id_vendor(dev, 0xAB00);
  libevdev_set_id_product(dev, 0xAB02);
  libevdev_set_id_version(dev, 0xAB00);
  libevdev_set_id_bustype(dev, BUS_USB);

  libevdev_enable_property(dev, INPUT_PROP_DIRECT);
  libevdev_enable_event_type(dev, EV_KEY);
  libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, nullptr);

  struct input_absinfo absinfo {
    .value = 0, .minimum = 0, .maximum = 65535, .fuzz = 1, .flat = 0, .resolution = 28
  };
  libevdev_enable_event_type(dev, EV_ABS);

  absinfo.maximum = ABS_MAX_WIDTH;
  libevdev_enable_event_code(dev, EV_ABS, ABS_X, &absinfo);
  absinfo.maximum = ABS_MAX_HEIGHT;
  libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &absinfo);

  auto err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  if (err != 0) {
    logs::log(logs::error, "Unable to create mouse device, error code: {}", strerror(-err));
    return {};
  }

  logs::log(logs::debug, "[INPUT] Created virtual touchpad {}", libevdev_uinput_get_devnode(uidev));

  return uidev;
}

Mouse::Mouse() {
  this->_state = std::make_shared<MouseState>();

  libevdev_ptr mouse_dev(libevdev_new(), ::libevdev_free);
  if (auto mouse_el = create_mouse(mouse_dev.get())) {
    this->_state->mouse_rel = {*mouse_el, ::libevdev_uinput_destroy};
  }

  libevdev_ptr mouse_abs_dev(libevdev_new(), ::libevdev_free);
  if (auto touch_el = create_mouse_abs(mouse_abs_dev.get())) {
    this->_state->mouse_abs = {*touch_el, ::libevdev_uinput_destroy};
  }
}

Mouse::~Mouse() {}

void Mouse::move(int delta_x, int delta_y) {
  if (auto mouse = _state->mouse_rel.get()) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_X, delta_x);
    libevdev_uinput_write_event(mouse, EV_REL, REL_Y, delta_y);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }
}

void Mouse::move_abs(int x, int y, int screen_width, int screen_height) {
  int scaled_x = (int)std::lround((ABS_MAX_WIDTH / screen_width) * x);
  int scaled_y = (int)std::lround((ABS_MAX_HEIGHT / screen_height) * y);

  if (auto mouse = _state->mouse_abs.get()) {
    libevdev_uinput_write_event(mouse, EV_ABS, ABS_X, scaled_x);
    libevdev_uinput_write_event(mouse, EV_ABS, ABS_Y, scaled_y);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }
}

static std::pair<int, int> btn_to_uinput(Mouse::MOUSE_BUTTON button) {
  switch (button) {
  case Mouse::LEFT:
    return {BTN_LEFT, 90001};
  case Mouse::MIDDLE:
    return {BTN_MIDDLE, 90003};
  case Mouse::RIGHT:
    return {BTN_RIGHT, 90002};
  case Mouse::SIDE:
    return {BTN_SIDE, 90004};
  default:
    return {BTN_EXTRA, 90005};
  }
}

void Mouse::press(Mouse::MOUSE_BUTTON button) {
  if (auto mouse = _state->mouse_rel.get()) {
    auto [btn_type, scan_code] = btn_to_uinput(button);
    libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan_code);
    libevdev_uinput_write_event(mouse, EV_KEY, btn_type, 1);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }
}

void Mouse::release(Mouse::MOUSE_BUTTON button) {
  if (auto mouse = _state->mouse_rel.get()) {
    auto [btn_type, scan_code] = btn_to_uinput(button);
    libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan_code);
    libevdev_uinput_write_event(mouse, EV_KEY, btn_type, 0);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }
}

void Mouse::horizontal_scroll(int amount) {
  int high_res_distance = amount;
  int distance = high_res_distance / 120;

  if (auto mouse = _state->mouse_rel.get()) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_HWHEEL, distance);
    libevdev_uinput_write_event(mouse, EV_REL, REL_HWHEEL_HI_RES, high_res_distance);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }
}

void Mouse::vertical_scroll(int amount) {
  int high_res_distance = amount;
  int distance = high_res_distance / 120;

  if (auto mouse = _state->mouse_rel.get()) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL, distance);
    libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL_HI_RES, high_res_distance);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }
}

} // namespace wolf::core::input