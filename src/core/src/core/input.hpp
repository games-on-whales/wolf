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
  Mouse(inputtino::Mouse &&j) noexcept : inputtino::Mouse(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class Trackpad : public inputtino::Trackpad, public VirtualDevice {
public:
  Trackpad(inputtino::Trackpad &&j) noexcept : inputtino::Trackpad(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class TouchScreen : public inputtino::TouchScreen, public VirtualDevice {
public:
  TouchScreen(inputtino::TouchScreen &&j) noexcept : inputtino::TouchScreen(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class PenTablet : public inputtino::PenTablet, public VirtualDevice {
public:
  PenTablet(inputtino::PenTablet &&j) noexcept : inputtino::PenTablet(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class Keyboard : public inputtino::Keyboard, public VirtualDevice {
public:
  Keyboard(inputtino::Keyboard &&j) noexcept : inputtino::Keyboard(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class XboxOneJoypad : public inputtino::XboxOneJoypad, public VirtualDevice {
public:
  XboxOneJoypad(inputtino::XboxOneJoypad &&j) noexcept : inputtino::XboxOneJoypad(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class SwitchJoypad : public inputtino::SwitchJoypad, public VirtualDevice {
public:
  SwitchJoypad(inputtino::SwitchJoypad &&j) noexcept : inputtino::SwitchJoypad(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};

class PS5Joypad : public inputtino::PS5Joypad, public VirtualDevice {
public:
  PS5Joypad(inputtino::PS5Joypad &&j) noexcept : inputtino::PS5Joypad(std::move(j)) {}

  std::vector<std::map<std::string, std::string>> get_udev_events() const override;
  std::vector<std::pair<std::string, std::vector<std::string>>> get_udev_hw_db_entries() const override;
};
} // namespace wolf::core::input
