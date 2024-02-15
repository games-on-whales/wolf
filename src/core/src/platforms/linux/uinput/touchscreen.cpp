#include "uinput.hpp"
#include <inputtino/protected_types.hpp>

namespace wolf::core::input {

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
} // namespace wolf::core::input