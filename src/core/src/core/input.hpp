#pragma once

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <chrono>
#include <cstdint>
#include <inputtino/input.hpp>
#include <map>
#include <optional>
#include <thread>
#include <vector>

namespace wolf::core::input {

using namespace std::chrono_literals;

class VirtualDevice {
public:
  virtual std::vector<std::map<std::string, std::string>> get_udev_events() const = 0;
  virtual std::vector<std::pair<std::string, /* filename */ std::vector<std::string> /* file rows */>>
  get_udev_hw_db_entries() const = 0;
};

/**
 * A virtual mouse device
 */
class Mouse : public inputtino::Mouse, public VirtualDevice {
public:
  Mouse(const inputtino::Mouse &m) : inputtino::Mouse(m) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class Trackpad : public inputtino::Trackpad, public VirtualDevice {
public:
  Trackpad(const inputtino::Trackpad &t) : inputtino::Trackpad(t) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class TouchScreen : public inputtino::TouchScreen, public VirtualDevice {
public:
  TouchScreen(const inputtino::TouchScreen &t) : inputtino::TouchScreen(t) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class PenTablet : public inputtino::PenTablet, public VirtualDevice {
public:
  PenTablet(const inputtino::PenTablet &t) : inputtino::PenTablet(t) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class Keyboard : public inputtino::Keyboard, public VirtualDevice {
public:
  Keyboard(const inputtino::Keyboard &k) : inputtino::Keyboard(k) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class Joypad : public inputtino::Joypad, public VirtualDevice {
public:
  Joypad(const inputtino::Joypad &j) : inputtino::Joypad(j) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};
} // namespace wolf::core::input
