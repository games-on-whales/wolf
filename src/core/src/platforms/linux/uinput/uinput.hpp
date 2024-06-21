/**
 * This is all based on libevdev
 *  - Here's a great introductory blog post:
 * https://web.archive.org/web/20200809000852/https://who-t.blogspot.com/2016/09/understanding-evdev.html/
 *  - Main docs: https://www.freedesktop.org/software/libevdev/doc/latest/index.html
 *  - Python docs are also of good quality: https://python-libevdev.readthedocs.io/en/latest/index.html
 *
 * You can debug your system using `evemu-describe`, `evemu-record` and `udevadm monitor`
 * (they can be installed using: `apt install -y evemu-tools`)
 *
 * For controllers there's a set of tools in the `joystick` package:
 * - ffcfstress  - force-feedback stress test
 * - ffmvforce   - force-feedback orientation test
 * - ffset       - force-feedback configuration tool
 * - fftest      - general force-feedback test
 * - jstest      - joystick test
 * - jscal       - joystick calibration tool
 *
 * For force feedback see: https://www.kernel.org/doc/html/latest/input/ff.html
 */
#pragma once

#include <chrono>
#include <core/input.hpp>
#include <filesystem>
#include <helpers/logger.hpp>
#include <inputtino/protected_types.hpp>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libudev.h>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace wolf::core::input {

using libevdev_ptr = std::shared_ptr<libevdev>;

/**
 * Given a device will read all queued events available at this time up to max_events
 * It'll automatically discard all EV_SYN events
 *
 * @returns a list of smart pointers of evdev input_event (empty when no events are available)
 */
std::vector<inputtino::libevdev_event_ptr> fetch_events(const libevdev_ptr &dev, int max_events = 50);

static std::pair<unsigned int, unsigned int> get_major_minor(const std::string &devnode) {
  struct stat buf {};
  if (stat(devnode.c_str(), &buf) == -1) {
    logs::log(logs::warning, "Unable to get stats of {}", devnode);
    return {};
  }

  if (!S_ISCHR(buf.st_mode)) {
    logs::log(logs::warning, "Device {} is not a character device", devnode);
    return {};
  }

  return {major(buf.st_rdev), minor(buf.st_rdev)};
}

static std::string gen_udev_hw_db_filename(std::string dev_node) {
  auto [dev_major, dev_minor] = get_major_minor(dev_node);
  auto filename = fmt::format("c{}:{}", dev_major, dev_minor);
  return filename;
}

static std::string gen_udev_hw_db_filename(inputtino::libevdev_uinput_ptr node) {
  return gen_udev_hw_db_filename(libevdev_uinput_get_devnode(node.get()));
}

static std::map<std::string, std::string>
gen_udev_base_event(const std::string &devnode, const std::string &syspath, const std::string &action = "add") {
  // Get major:minor
  auto [dev_major, dev_minor] = get_major_minor(devnode);

  // Current timestamp
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return {
      {"ACTION", action},
      {"SEQNUM", "7"}, // We don't want to keep global state, let's hope it's not used
      {"USEC_INITIALIZED", std::to_string(timestamp)},
      {"SUBSYSTEM", "input"},
      {"ID_INPUT", "1"},
      {"ID_SERIAL", "noserial"},
      {"TAGS", ":seat:uaccess:"},
      {"CURRENT_TAGS", ":seat:uaccess:"},
      {"DEVNAME", devnode},
      {"DEVPATH", syspath},
      {"MAJOR", std::to_string(dev_major)},
      {"MINOR", std::to_string(dev_minor)},
  };
}

static std::map<std::string, std::string> gen_udev_base_event(inputtino::libevdev_uinput_ptr node,
                                                              const std::string &action = "add") {

  // Get paths
  auto devnode = libevdev_uinput_get_devnode(node.get());
  std::string syspath = libevdev_uinput_get_syspath(node.get());
  syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
  syspath.append("/" + std::filesystem::path(devnode).filename().string()); // Adds /eventXY

  return gen_udev_base_event(devnode, syspath, action);
}

static std::map<std::string, std::string> gen_udev_base_device_event(inputtino::libevdev_uinput_ptr node,
                                                                     const std::string &action = "add") {
  std::string syspath = libevdev_uinput_get_syspath(node.get());
  syspath.erase(0, 4); // Remove leading /sys/ from syspath TODO: what if it's not /sys/?
  // Current timestamp
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return {
      {"ACTION", action},
      {"SEQNUM", "7"}, // We don't want to keep global state, let's hope it's not used
      {"USEC_INITIALIZED", std::to_string(timestamp)},
      {"SUBSYSTEM", "input"},
      {"ID_INPUT", "1"},
      {"ID_SERIAL", "noserial"},
      {"TAGS", ":seat:uaccess:"},
      {"CURRENT_TAGS", ":seat:uaccess:"},
      {"DEVPATH", syspath},
  };
}

} // namespace wolf::core::input
