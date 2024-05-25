#include "uinput.hpp"
#include <inputtino/protected_types.hpp>

namespace wolf::core::input {

std::vector<std::map<std::string, std::string>> XboxOneJoypad::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (_state->joy.get()) {
    // eventXY and jsX devices
    for (const auto &devnode : this->get_nodes()) {
      std::string syspath = libevdev_uinput_get_syspath(_state->joy.get());
      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX

      auto event = gen_udev_base_event(devnode, syspath);
      event["ID_INPUT_JOYSTICK"] = "1";
      event[".INPUT_CLASS"] = "joystick";
      //      event["UNIQ"] = UNIQ_ID;
      events.emplace_back(event);
    }
  }
  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> XboxOneJoypad::get_udev_hw_db_entries() const {
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
  return result;
}

std::vector<std::map<std::string, std::string>> SwitchJoypad::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  if (_state->joy.get()) {
    // eventXY and jsX devices
    for (const auto &devnode : this->get_nodes()) {
      std::string syspath = libevdev_uinput_get_syspath(_state->joy.get());
      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX

      auto event = gen_udev_base_event(devnode, syspath);
      event["ID_INPUT_JOYSTICK"] = "1";
      event[".INPUT_CLASS"] = "joystick";
      //      event["UNIQ"] = UNIQ_ID;
      events.emplace_back(event);
    }
  }
  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> SwitchJoypad::get_udev_hw_db_entries() const {
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
  return result;
}

std::vector<std::map<std::string, std::string>> PS5Joypad::get_udev_events() const {
  std::vector<std::map<std::string, std::string>> events;

  //  if (auto joy = _state->joy.get()) {
  //    // eventXY and jsX devices
  //    for (const auto &devnode : get_child_dev_nodes(joy)) {
  //      std::string syspath = libevdev_uinput_get_syspath(joy);
  //      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
  //      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX
  //
  //      auto event = gen_udev_base_event(devnode, syspath);
  //      event["ID_INPUT_JOYSTICK"] = "1";
  //      event[".INPUT_CLASS"] = "joystick";
  //      //      event["UNIQ"] = UNIQ_ID;
  //      events.emplace_back(event);
  //    }
  //  }
  //
  //  if (auto trackpad = _state->trackpad) {
  //    auto event = gen_udev_base_event(_state->trackpad->get_nodes()[0], ""); // TODO: syspath?
  //    event["ID_INPUT_TOUCHPAD"] = "1";
  //    event[".INPUT_CLASS"] = "mouse";
  //    events.emplace_back(event);
  //  }
  //
  //  if (auto motion_sensor = _state->motion_sensor.get()) {
  //    for (const auto &devnode : get_child_dev_nodes(motion_sensor)) {
  //      std::string syspath = libevdev_uinput_get_syspath(motion_sensor);
  //      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
  //      syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /jsX
  //
  //      auto event = gen_udev_base_event(devnode, syspath);
  //      event["ID_INPUT_ACCELEROMETER"] = "1";
  //      event["ID_INPUT_WIDTH_MM"] = "8";
  //      event["ID_INPUT_HEIGHT_MM"] = "8";
  //      //      event["UNIQ"] = UNIQ_ID;
  //      event["IIO_SENSOR_PROXY_TYPE"] = "input-accel";
  //      event["SYSTEMD_WANTS"] = "iio-sensor-proxy.service";
  //      events.emplace_back(event);
  //    }
  //  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> PS5Joypad::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

//  if (_state->joy.get()) {
//    result.push_back({gen_udev_hw_db_filename(_state->joy),
//                      {"E:ID_INPUT=1",
//                       "E:ID_INPUT_JOYSTICK=1",
//                       "E:ID_BUS=usb",
//                       "G:seat",
//                       "G:uaccess",
//                       "Q:seat",
//                       "Q:uaccess",
//                       "V:1"}});
//  }

  //  if (auto trackpad = _state->trackpad) {
  //    result.push_back({gen_udev_hw_db_filename(_state->trackpad->get_nodes()[0]),
  //                      {"E:ID_INPUT=1",
  //                       "E:ID_INPUT_TOUCHPAD=1",
  //                       "E:ID_BUS=usb",
  //                       "G:seat",
  //                       "G:uaccess",
  //                       "Q:seat",
  //                       "Q:uaccess",
  //                       "V:1"}});
  //  }
  //
  //  if (_state->motion_sensor.get()) {
  //    result.push_back({gen_udev_hw_db_filename(_state->motion_sensor),
  //                      {"E:ID_INPUT=1",
  //                       "E:ID_INPUT_ACCELEROMETER=1",
  //                       "E:ID_BUS=usb",
  //                       "G:seat",
  //                       "G:uaccess",
  //                       "Q:seat",
  //                       "Q:uaccess",
  //                       "V:1"}});
  //  }

  return result;
}

} // namespace wolf::core::input