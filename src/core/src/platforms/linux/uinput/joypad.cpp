#include "uinput.hpp"
#include <inputtino/protected_types.hpp>

namespace wolf::core::input {

namespace {

constexpr auto SWITCH_VENDOR_ID = "057e";
constexpr auto SWITCH_PRODUCT_ID = "2009";

std::string uppercase_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::map<std::string, std::string> read_uevent_properties(const std::filesystem::path &uevent_path) {
  std::map<std::string, std::string> props;
  std::ifstream uevent_file(uevent_path);
  std::string line;
  while (std::getline(uevent_file, line)) {
    if (auto split = line.find('='); split != std::string::npos) {
      auto value = line.substr(split + 1);
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      props.emplace(line.substr(0, split), value);
    }
  }
  return props;
}

std::vector<std::string> switch_udev_identity(const std::map<std::string, std::string> &identity) {
  std::vector<std::string> rows = {
      "E:ID_BUS=bluetooth",
      "E:HID_NAME=Pro Controller",
      "E:HID_ID=0005:0000057E:00002009",
      "E:ID_VENDOR_ID=057e",
      "E:ID_MODEL_ID=2009",
      "E:ID_VENDOR=Nintendo",
      "E:ID_MODEL=Pro_Controller",
      "E:ID_SERIAL=Nintendo_Pro_Controller",
  };

  for (const auto &[key, value] : identity) {
    if (!value.empty()) {
      rows.push_back(fmt::format("E:{}={}", key, value));
    }
  }
  return rows;
}

void append_switch_identity(std::vector<std::string> &entry, const std::map<std::string, std::string> &identity) {
  auto rows = switch_udev_identity(identity);
  entry.insert(entry.end(), rows.begin(), rows.end());
}

std::map<std::string, std::string> switch_input_identity(const std::filesystem::path &sys_entry,
                                                         const std::string &fallback_uniq) {
  auto props = read_uevent_properties(sys_entry / "uevent");
  const auto uniq = uppercase_ascii(props.contains("UNIQ") ? props.at("UNIQ") : fallback_uniq);
  return {
      {"UNIQ", uniq},
      {"HID_UNIQ", uniq},
      {"PHYS", props.contains("PHYS") ? props.at("PHYS") : "bluetooth"},
      {"MODALIAS", props.contains("MODALIAS") ? props.at("MODALIAS") : "hid:b0005g0001v0000057Ep00002009"},
  };
}

std::map<std::string, std::string> switch_hid_identity(const std::filesystem::path &base_path,
                                                       const std::string &fallback_uniq) {
  auto props = read_uevent_properties(base_path / "uevent");
  const auto uniq = uppercase_ascii(props.contains("HID_UNIQ") ? props.at("HID_UNIQ") : fallback_uniq);
  return {
      {"HID_UNIQ", uniq},
      {"HID_PHYS", props.contains("HID_PHYS") ? props.at("HID_PHYS") : "bluetooth"},
      {"MODALIAS", props.contains("MODALIAS") ? props.at("MODALIAS") : "hid:b0005g0001v0000057Ep00002009"},
  };
}

void append_switch_identity(std::map<std::string, std::string> &event,
                            const std::map<std::string, std::string> &identity) {
  for (const auto &[key, value] : identity) {
    if (!value.empty()) {
      event[key] = value;
    }
  }

  event["ID_BUS"] = "bluetooth";
  event["HID_NAME"] = "Pro Controller";
  event["HID_ID"] = "0005:0000057E:00002009";
  event["ID_VENDOR_ID"] = SWITCH_VENDOR_ID;
  event["ID_MODEL_ID"] = SWITCH_PRODUCT_ID;
}

std::map<std::string, std::string> switch_legacy_identity(const std::string &uniq) {
  const auto canonical_uniq = uppercase_ascii(uniq);
  return {
      {"UNIQ", canonical_uniq},
      {"HID_UNIQ", canonical_uniq},
      {"PHYS", "bluetooth"},
      {"HID_PHYS", "bluetooth"},
      {"MODALIAS", "hid:b0005g0001v0000057Ep00002009"},
  };
}

void append_switch_legacy_identity(std::vector<std::string> &entry, const std::string &uniq) {
  auto identity = switch_legacy_identity(uniq);
  auto rows = switch_udev_identity(identity);
  entry.insert(entry.end(), rows.begin(), rows.end());
}

} // namespace

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

  auto sys_nodes = this->get_sys_nodes();
  if (sys_nodes.empty() && _state->joy.get()) {
    for (const auto &devnode : this->get_nodes()) {
      std::string syspath = libevdev_uinput_get_syspath(_state->joy.get());
      syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      syspath.append("/" + std::filesystem::path(devnode).filename().string());

      auto event = gen_udev_base_event(devnode, syspath);
      event["ID_INPUT_JOYSTICK"] = "1";
      event[".INPUT_CLASS"] = "joystick";
      append_switch_identity(event, switch_legacy_identity(this->get_mac_address()));
      events.emplace_back(event);
    }
    return events;
  }

  for (const auto &sys_entry : sys_nodes) {
    for (const auto &sys_node : std::filesystem::directory_iterator{sys_entry}) {
      if (!sys_node.is_directory() || sys_node.path().filename().string().rfind("event", 0) != 0) {
        continue;
      }

      auto sys_path = sys_node.path().string();
      sys_path.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
      auto dev_path = ("/dev/input/" / sys_node.path().filename()).string();
      auto event = gen_udev_base_event(dev_path, sys_path);
      auto identity = switch_input_identity(sys_entry, this->get_mac_address());

      std::ifstream name_file(std::filesystem::path(sys_entry) / "name");
      std::string name;
      std::getline(name_file, name);

      if (name.find("Motion") != std::string::npos || name.find("IMU") != std::string::npos) {
        event["ID_INPUT_ACCELEROMETER"] = "1";
        event["ID_INPUT_WIDTH_MM"] = "8";
        event["ID_INPUT_HEIGHT_MM"] = "8";
        event["IIO_SENSOR_PROXY_TYPE"] = "input-accel";
        event["SYSTEMD_WANTS"] = "iio-sensor-proxy.service";
        event["UNIQ"] = this->get_mac_address();
      } else {
        // Skip the evdev joystick node — SDL's HIDAPI backend handles the Switch
        // Pro Controller via hidraw with correct button remapping and gyro support.
        // Exposing the evdev joystick causes SDL to create a duplicate controller
        // with wrong button mapping and no gyro.
        continue;
      }
      append_switch_identity(event, identity);

      events.emplace_back(event);
    }
  }

  if (!sys_nodes.empty()) {
    auto base_path = std::filesystem::path(sys_nodes[0]).parent_path().parent_path();
    if (std::filesystem::exists(base_path / "hidraw")) {
      for (const auto &hidraw_entry : std::filesystem::directory_iterator{base_path / "hidraw"}) {
        auto dev_path = "/dev/" + hidraw_entry.path().filename().string();
        auto sys_path = hidraw_entry.path().string();
        sys_path.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?

        auto event = gen_udev_base_event(dev_path, sys_path);
        event["SUBSYSTEM"] = "hidraw";
        append_switch_identity(event, switch_hid_identity(base_path, this->get_mac_address()));
        events.emplace_back(event);
      }
    }
  }

  return events;
}

std::vector<std::pair<std::string, std::vector<std::string>>> SwitchJoypad::get_udev_hw_db_entries() const {
  std::vector<std::pair<std::string, std::vector<std::string>>> result;
  const auto uniq = this->get_mac_address();

  if (this->get_sys_nodes().empty() && _state->joy.get()) {
    auto entry = std::vector<std::string>{
        "E:ID_INPUT=1",
        "E:ID_INPUT_JOYSTICK=1",
        "G:seat",
        "G:uaccess",
        "Q:seat",
        "Q:uaccess",
        "V:1",
    };
    append_switch_legacy_identity(entry, uniq);
    result.push_back({gen_udev_hw_db_filename(_state->joy), entry});
    return result;
  }

  for (const auto &sys_entry : this->get_sys_nodes()) {
    for (const auto &sys_node : std::filesystem::directory_iterator{sys_entry}) {
      if (!sys_node.is_directory() || sys_node.path().filename().string().rfind("event", 0) != 0) {
        continue;
      }

      auto dev_path = ("/dev/input/" / sys_node.path().filename()).string();
      std::pair<std::string, std::vector<std::string>> entry;
      entry.first = gen_udev_hw_db_filename(dev_path);

      std::ifstream name_file(std::filesystem::path(sys_entry) / "name");
      std::string name;
      std::getline(name_file, name);
      auto identity = switch_input_identity(sys_entry, uniq);

      if (name.find("Motion") != std::string::npos || name.find("IMU") != std::string::npos) {
        entry.second =
            {"E:ID_INPUT=1", "E:ID_INPUT_ACCELEROMETER=1", "G:seat", "G:uaccess", "Q:seat", "Q:uaccess", "V:1"};
      } else {
        // Skip evdev joystick hw_db entry — HIDAPI handles the controller via hidraw.
        continue;
      }
      append_switch_identity(entry.second, identity);

      result.emplace_back(entry);
    }
  }

  auto sys_nodes = this->get_sys_nodes();
  if (!sys_nodes.empty()) {
    auto base_path = std::filesystem::path(sys_nodes[0]).parent_path().parent_path();
    if (std::filesystem::exists(base_path / "hidraw")) {
      for (const auto &hidraw_entry : std::filesystem::directory_iterator{base_path / "hidraw"}) {
        auto dev_path = "/dev/" + hidraw_entry.path().filename().string();
        auto entry = std::vector<std::string>{
            "E:SUBSYSTEM=hidraw",
            "G:seat",
            "G:uaccess",
            "Q:seat",
            "Q:uaccess",
            "V:1",
        };
        append_switch_identity(entry, switch_hid_identity(base_path, uniq));
        result.push_back({gen_udev_hw_db_filename(dev_path), entry});
      }
    }
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
