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

  auto sys_nodes = this->get_sys_nodes();
  for (const auto sys_entry : sys_nodes) {
    auto input_nodes = std::filesystem::directory_iterator{sys_entry};

    for (auto sys_node : input_nodes) {
      if (sys_node.is_directory() && (sys_node.path().filename().string().rfind("event", 0) == 0 ||
                                      sys_node.path().filename().string().rfind("mouse", 0) == 0 ||
                                      sys_node.path().filename().string().rfind("js", 0) == 0)) {
        auto sys_path = sys_node.path().string();
        sys_path.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
        auto dev_path = ("/dev/input/" / sys_node.path().filename()).string();
        auto event = gen_udev_base_event(dev_path, sys_path);

        // Check the name of the device to determine the type
        std::ifstream name_file(std::filesystem::path(sys_entry) / "name");
        std::string name;
        std::getline(name_file, name);
        if (name.find("Touchpad") != std::string::npos) { // touchpad
          event["ID_INPUT_TOUCHPAD"] = "1";
          event[".INPUT_CLASS"] = "mouse";
          event["ID_INPUT_TOUCHPAD_INTEGRATION"] = "internal";
        } else if (name.find("Motion") != std::string::npos) { // gyro + acc
          event["ID_INPUT_ACCELEROMETER"] = "1";
          event["ID_INPUT_WIDTH_MM"] = "8";
          event["ID_INPUT_HEIGHT_MM"] = "8";
          event["IIO_SENSOR_PROXY_TYPE"] = "input-accel";
          event["SYSTEMD_WANTS"] = "iio-sensor-proxy.service";
          event["UNIQ"] = this->get_mac_address();
        } else { // joypad
          event["ID_INPUT_JOYSTICK"] = "1";
          event[".INPUT_CLASS"] = "joystick";
          event["UNIQ"] = this->get_mac_address();
        }

        events.emplace_back(event);
      }
    }
  }

  if (!sys_nodes.empty()) {
    // Add /dev/hidraw* device
    // Used by Steam to access the LED status and who knows what else...
    auto base_path =
        std::filesystem::path(sys_nodes[0]) // /sys/devices/virtual/misc/uhid/0003:054C:0CE6.0016/input/input158
            .parent_path()                  // "/sys/devices/virtual/misc/uhid/0003:054C:0CE6.0016/input/
            .parent_path();                 // "/sys/devices/virtual/misc/uhid/0003:054C:0CE6.0016/

    if (std::filesystem::exists(base_path / "hidraw")) {
      auto hidraw_entries = std::filesystem::directory_iterator{base_path / "hidraw"};
      for (auto hidraw_entry : hidraw_entries) {
        auto dev_path = "/dev/" + hidraw_entry.path().filename().string();
        auto sys_path = hidraw_entry.path().string();
        sys_path.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?

        auto event = gen_udev_base_event(dev_path, sys_path);
        event["SUBSYSTEM"] = "hidraw";
        events.emplace_back(event);
      }
    } else {
      logs::log(logs::warning, "Unable to find HIDRAW nodes for PS5 joypad under {}", base_path.string());
    }
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> PS5Joypad::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;

  for (const auto sys_entry : this->get_sys_nodes()) {
    auto sys_nodes = std::filesystem::directory_iterator{sys_entry};

    for (auto sys_node : sys_nodes) {
      if (sys_node.is_directory() && (sys_node.path().filename().string().rfind("event", 0) == 0 ||
                                      sys_node.path().filename().string().rfind("js", 0) == 0 ||
                                      sys_node.path().filename().string().rfind("mouse", 0) == 0)) {
        auto sys_path = sys_node.path().string();
        sys_path.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
        auto dev_path = ("/dev/input/" / sys_node.path().filename()).string();

        std::pair<std::string, std::vector<std::string>> entry;
        entry.first = gen_udev_hw_db_filename(dev_path);

        // Check the name of the device to determine the type
        std::ifstream name_file(std::filesystem::path(sys_entry) / "name");
        std::string name;
        std::getline(name_file, name);
        if (name.find("Touchpad") != std::string::npos) { // touchpad
          entry.second = {"E:ID_INPUT=1",
                          "E:ID_INPUT_TOUCHPAD=1",
                          "E:ID_BUS=usb",
                          "G:seat",
                          "G:uaccess",
                          "Q:seat",
                          "Q:uaccess",
                          "V:1"};
        } else if (name.find("Motion") != std::string::npos) { // gyro + acc
          entry.second = {"E:ID_INPUT=1",
                          "E:ID_INPUT_ACCELEROMETER=1",
                          "E:ID_BUS=usb",
                          "G:seat",
                          "G:uaccess",
                          "Q:seat",
                          "Q:uaccess",
                          "V:1"};
        } else { // joypad
          entry.second = {"E:ID_INPUT=1",
                          "E:ID_INPUT_JOYSTICK=1",
                          "E:ID_BUS=usb",
                          "G:seat",
                          "G:uaccess",
                          "Q:seat",
                          "Q:uaccess",
                          "V:1"};
        }

        result.emplace_back(entry);
      }
    }
  }

  return result;
}

} // namespace wolf::core::input