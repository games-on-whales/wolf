#pragma once

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <chrono>
#include <cstdint>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <optional>
#include <thread>

namespace wolf::core::input {

using namespace std::chrono_literals;

class VirtualDevice {
public:
  virtual std::vector<std::string> get_nodes() const = 0;
  virtual ~VirtualDevice() = default;
};

/**
 * A virtual mouse device
 */
class Mouse : public VirtualDevice {
protected:
  typedef struct MouseState MouseState;

private:
  std::shared_ptr<MouseState> _state;

public:
  Mouse();
  Mouse(const Mouse &j) : _state(j._state) {}
  Mouse(Mouse &&j) : _state(std::move(j._state)) {}
  ~Mouse() override;

  std::vector<std::string> get_nodes() const override;

  void move(int delta_x, int delta_y);

  void move_abs(int x, int y, int screen_width, int screen_height);

  enum MOUSE_BUTTON {
    LEFT,
    MIDDLE,
    RIGHT,
    SIDE,
    EXTRA
  };

  void press(MOUSE_BUTTON button);

  void release(MOUSE_BUTTON button);

  void vertical_scroll(int amount);

  void horizontal_scroll(int amount);
};

/**
 * A virtual touchpad
 * TODO
 */
// class Touchpad {
// public:
//   Touchpad();
//
//   void touch(int x, int y, int pressure);
// };

/**
 * TODO: better name?
 */
// class DrawingTablet {
//   DrawingTablet();
//
//   void press(int x, int y, int pressure, uint8_t buttons, uint8_t tool_type, uint16_t rotation, uint8_t tilt);
// };

/**
 * A virtual keyboard device
 *
 * Key codes are Win32 Virtual Key (VK) codes
 * Users of this class can expect that if a key is pressed, it'll be re-pressed every
 * time_repress_key until it's released.
 */
class Keyboard : public VirtualDevice {
protected:
  typedef struct KeyboardState KeyboardState;

private:
  std::shared_ptr<KeyboardState> _state;

public:
  explicit Keyboard(std::chrono::milliseconds timeout_repress_key = 50ms);

  // A keyboard can't be copied, you can only create a new one
  Keyboard(const Keyboard &j) = delete;
  Keyboard(Keyboard &&j) = delete;
  void operator=(const Keyboard &j) = delete;

  ~Keyboard() override;

  std::vector<std::string> get_nodes() const override;

  void press(short key_code);

  void release(short key_code);

  /**
   * Here we receive a single UTF-8 encoded char at a time,
   * the trick is to convert it to UTF-32 then send CTRL+SHIFT+U+<HEXCODE> in order to produce any
   * unicode character, see: https://en.wikipedia.org/wiki/Unicode_input
   *
   * ex:
   * - when receiving UTF-8 [0xF0 0x9F 0x92 0xA9] (which is '💩')
   * - we'll convert it to UTF-32 [0x1F4A9]
   * - then type: CTRL+SHIFT+U+1F4A9
   * see the conversion at: https://www.compart.com/en/unicode/U+1F4A9
   */
  void paste_utf(const std::basic_string<char32_t> &utf32);
};

/**
 * An abstraction on top of a virtual joypad
 * In order to support callbacks (ex: on_rumble()) this will create a new thread for listening for such events
 */
class Joypad : public VirtualDevice {
protected:
  typedef struct JoypadState JoypadState;

private:
  std::shared_ptr<JoypadState> _state;

public:
  enum CONTROLLER_TYPE : uint8_t {
    UNKNOWN = 0x00,
    XBOX = 0x01,
    PS = 0x02,
    NINTENDO = 0x03
  };

  enum CONTROLLER_CAPABILITIES : uint8_t {
    ANALOG_TRIGGERS = 0x01,
    RUMBLE = 0x02,
    TRIGGER_RUMBLE = 0x04,
    TOUCHPAD = 0x08,
    ACCELEROMETER = 0x10,
    GYRO = 0x20,
    BATTERY = 0x40,
    RGB_LED = 0x80
  };

  Joypad(CONTROLLER_TYPE type, uint8_t capabilities);

  // A joypad can't be copied, you can only create a new one
  Joypad(const Joypad &j) = delete;
  Joypad(Joypad &&j) = delete;
  void operator=(const Joypad &j) = delete;

  ~Joypad() override;

  std::vector<std::string> get_nodes() const override;

  enum CONTROLLER_BTN : int {
    DPAD_UP = 0x0001,
    DPAD_DOWN = 0x0002,
    DPAD_LEFT = 0x0004,
    DPAD_RIGHT = 0x0008,

    START = 0x0010,
    BACK = 0x0020,
    HOME = 0x0400,

    LEFT_STICK = 0x0040,
    RIGHT_STICK = 0x0080,
    LEFT_BUTTON = 0x0100,
    RIGHT_BUTTON = 0x0200,

    SPECIAL_FLAG = 0x0400,
    PADDLE1_FLAG = 0x010000,
    PADDLE2_FLAG = 0x020000,
    PADDLE3_FLAG = 0x040000,
    PADDLE4_FLAG = 0x080000,
    TOUCHPAD_FLAG = 0x100000, // Touchpad buttons on Sony controllers
    MISC_FLAG = 0x200000,     // Share/Mic/Capture/Mute buttons on various controllers

    A = 0x1000,
    B = 0x2000,
    X = 0x4000,
    Y = 0x8000
  };

  /**
   * Given the nature of joypads we (might) have to simultaneously press and release multiple buttons.
   * In order to implement this, you can pass a single short: button_flags which represent the currently pressed
   * buttons in the joypad.
   * This class will keep an internal state of the joypad and will automatically release buttons that are no
   * longer pressed.
   *
   * Example: previous state had `DPAD_UP` and `A` -> user release `A` -> new state only has `DPAD_UP`
   */
  void set_pressed_buttons(int newly_pressed);

  void set_triggers(unsigned char left, unsigned char right);

  enum STICK_POSITION {
    R2,
    L2
  };

  void set_stick(STICK_POSITION stick_type, short x, short y);

  void set_on_rumble(const std::function<void(int low_freq, int high_freq)> &callback);

  /**
   * We expect (x,y) to be in the range [0.0, 1.0]; x and y values are normalised device coordinates
   * from the top-left corner (0.0, 0.0) to bottom-right corner (1.0, 1.0)
   */
  void touchpad_place_finger(int finger_nr, float x, float y);

  void touchpad_release_finger(int finger_nr);

  enum MOTION_TYPE : uint8_t {
    ACCELERATION = 0x01,
    GYROSCOPE = 0x02
  };

  void set_motion(MOTION_TYPE type, float x, float y, float z);

  enum BATTERY_STATE : uint8_t {
    NOT_KNOWN = 0x00,
    NOT_PRESENT = 0x01,
    DISCHARGING = 0x02,
    CHARGHING = 0x03,
    NOT_CHARGING = 0x04,
    FULL = 0x05
  };

  void set_battery(BATTERY_STATE state, int percentage);

  void set_on_led(const std::function<void(int r, int g, int b)> &callback);
};
} // namespace wolf::core::input
