#include "uinput.hpp"
#include <inputtino/protected_types.hpp>

namespace wolf::core::input {

std::vector<std::map<std::string, std::string>> PenTablet::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (_state->pen_tablet.get()) {
    auto event = gen_udev_base_event(_state->pen_tablet);
    event["ID_INPUT_TABLET"] = "1";
    events.emplace_back(event);
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> PenTablet::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

  if (_state->pen_tablet.get()) {
    result.push_back(
        {gen_udev_hw_db_filename(_state->pen_tablet),
         {"E:ID_INPUT=1", "E:ID_INPUT_TABLET=1", "E:ID_BUS=usb", "G:seat", "G:uaccess", "Q:seat", "Q:uaccess", "V:1"}});
  }

  return result;
}

} // namespace wolf::core::input