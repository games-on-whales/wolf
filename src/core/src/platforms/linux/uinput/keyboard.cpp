#include "uinput.hpp"
#include <algorithm>
#include <inputtino/protected_types.hpp>

namespace wolf::core::input {

using namespace std::string_literals;

std::vector<std::map<std::string, std::string>> Keyboard::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (_state->kb.get()) {
    auto event = gen_udev_base_event(_state->kb);
    event["ID_INPUT_KEYBOARD"] = "1";
    event[".INPUT_CLASS"] = "keyboard";
    events.emplace_back(event);
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> Keyboard::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

  if (_state->kb.get()) {
    // TODO: E:XKBMODEL=pc105 E:XKBLAYOUT=gb
    result.push_back({gen_udev_hw_db_filename(_state->kb),
                      {"E:ID_INPUT=1", "E:ID_INPUT_KEY=1", "E:ID_INPUT_KEYBOARD=1", "E:ID_SERIAL=noserial", "V:1"}});
  }

  return result;
}
} // namespace wolf::core::input