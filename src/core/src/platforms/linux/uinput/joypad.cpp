#include "uinput.hpp"
#include <inputtino/protected_types.hpp>
#include <linux/input.h>
#include <linux/uinput.h>
#include <optional>

namespace wolf::core::input {

/**
 * This needs to be the same for all the virtual devices in order for SDL to match gyro with the joypad
 * see:
 * https://github.com/libsdl-org/SDL/blob/7cc3e94eb22f2ee76742bfb4c101757fcb70c4b7/src/joystick/linux/SDL_sysjoystick.c#L1446
 */
static constexpr std::string_view UNIQ_ID = "00:11:22:33:44:55";

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
      }
      udev_device_unref(child_dev);
    }

    udev_enumerate_unref(enumerate);
    udev_device_unref(device_ptr);
  }

  udev_unref(udev);
  return result;
}

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

  if (auto trackpad = _state->trackpad) {
    auto event = gen_udev_base_event(_state->trackpad->get_nodes()[0], ""); // TODO: syspath?
    event["ID_INPUT_TOUCHPAD"] = "1";
    event[".INPUT_CLASS"] = "mouse";
    events.emplace_back(event);
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

  if (auto trackpad = _state->trackpad) {
    result.push_back({gen_udev_hw_db_filename(_state->trackpad->get_nodes()[0]),
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

} // namespace wolf::core::input