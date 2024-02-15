#include "uinput.hpp"
#include <helpers/logger.hpp>
#include <inputtino/protected_types.hpp>

namespace wolf::core::input {

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
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

  if (_state->mouse_rel.get()) {
    result.push_back({gen_udev_hw_db_filename(_state->mouse_rel),
                      {"E:ID_INPUT=1", "E:ID_INPUT_MOUSE=1", "E:ID_SERIAL=noserial", "V:1"}});
  }

  if (_state->mouse_abs.get()) {
    result.push_back({gen_udev_hw_db_filename(_state->mouse_abs),
                      {"E:ID_INPUT=1", "E:ID_INPUT_TOUCHPAD=1", "E:ID_SERIAL=noserial", "V:1"}});
  }

  return result;
}
} // namespace wolf::core::input