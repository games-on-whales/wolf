#include "catch2/catch_all.hpp"
#include <boost/endian/conversion.hpp>
#include <boost/locale.hpp>
#include <chrono>
#include <fcntl.h>
#include <platforms/linux/uinput/keyboard.hpp>
#include <platforms/linux/uinput/uinput.hpp>
#include <thread>

using Catch::Matchers::Equals;

using namespace wolf::core::input;
using namespace std::string_literals;

void link_devnode(libevdev *dev, libevdev_uinput *dev_input) {
  // We have to sleep in order to be able to read from the newly created device
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto fd = open(libevdev_uinput_get_devnode(dev_input), O_RDONLY | O_NONBLOCK);
  REQUIRE(fd >= 0);
  libevdev_set_fd(dev, fd);
}

TEST_CASE("uinput - keyboard", "UINPUT") {
  libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr keyboard_el = {keyboard::create_keyboard(keyboard_dev.get()).value(), ::libevdev_uinput_destroy};

  link_devnode(keyboard_dev.get(), keyboard_el.get());

  auto events = fetch_events(keyboard_dev);
  REQUIRE(events.empty());

  auto press_shift_key = data::pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0xA0)};
  press_shift_key.type = data::KEY_PRESS;

  auto press_action = keyboard::keyboard_handle(keyboard_el.get(), press_shift_key);
  REQUIRE(press_action->pressed);
  REQUIRE(press_action->linux_code == KEY_LEFTSHIFT);

  events = fetch_events(keyboard_dev);
  REQUIRE(events.size() == 1);
  REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_KEY"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("KEY_LEFTSHIFT"));
  REQUIRE(events[0]->value == 1);

  auto release_shift_key = data::pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0xA0)};
  release_shift_key.type = data::KEY_RELEASE;

  auto release_action = keyboard::keyboard_handle(keyboard_el.get(), release_shift_key);
  REQUIRE(!release_action->pressed);
  REQUIRE(release_action->linux_code == KEY_LEFTSHIFT);

  events = fetch_events(keyboard_dev);
  REQUIRE(events.size() == 1);
  REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_KEY"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("KEY_LEFTSHIFT"));
  REQUIRE(events[0]->value == 0);
}

TEST_CASE("uinput - mouse", "UINPUT") {
  libevdev_ptr mouse_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr mouse_el = {mouse::create_mouse(mouse_dev.get()).value(), ::libevdev_uinput_destroy};

  link_devnode(mouse_dev.get(), mouse_el.get());

  auto events = fetch_events(mouse_dev);
  REQUIRE(events.empty());

  SECTION("Mouse move") {
    auto mv_packet = data::pkts::MOUSE_MOVE_REL_PACKET{.delta_x = 10, .delta_y = 20};
    mouse::move_mouse(mouse_el.get(), mv_packet);

    events = fetch_events(mouse_dev);
    REQUIRE(events.size() == 2);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("REL_X"));
    REQUIRE(10 == mv_packet.delta_x);

    REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("REL_Y"));
    REQUIRE(20 == mv_packet.delta_y);
  }

  SECTION("Mouse press button") {
    auto pressed_packet = data::pkts::MOUSE_BUTTON_PACKET{.button = 5};
    pressed_packet.type = data::MOUSE_BUTTON_PRESS;
    mouse::mouse_press(mouse_el.get(), pressed_packet);

    events = fetch_events(mouse_dev);
    REQUIRE(events.size() == 2);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_MSC"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("MSC_SCAN"));
    REQUIRE(events[0]->value == 90005);

    REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_KEY"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("BTN_EXTRA"));
    REQUIRE(events[1]->value == 1);
  }

  SECTION("Mouse scroll") {
    short scroll_amt = 10;
    auto scroll_packet = data::pkts::MOUSE_SCROLL_PACKET{.scroll_amt1 = boost::endian::native_to_big(scroll_amt)};
    mouse::mouse_scroll(mouse_el.get(), scroll_packet);

    events = fetch_events(mouse_dev);
    REQUIRE(events.size() == 1);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("REL_WHEEL_HI_RES"));
    REQUIRE(events[0]->value == scroll_amt);
  }

  SECTION("Mouse horizontal scroll") {
    short scroll_amt = 10;
    auto scroll_packet = data::pkts::MOUSE_HSCROLL_PACKET{.scroll_amount = boost::endian::native_to_big(scroll_amt)};
    mouse::mouse_scroll_horizontal(mouse_el.get(), scroll_packet);

    events = fetch_events(mouse_dev);
    REQUIRE(events.size() == 1);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("REL_HWHEEL_HI_RES"));
    REQUIRE(events[0]->value == scroll_amt);
  }
}

TEST_CASE("uinput - touchpad", "UINPUT") {
  libevdev_ptr mouse_abs(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr touch_el = {mouse::create_mouse_abs(mouse_abs.get()).value(), ::libevdev_uinput_destroy};

  link_devnode(mouse_abs.get(), touch_el.get());

  auto events = fetch_events(mouse_abs);
  REQUIRE(events.empty());

  auto mv_packet = data::pkts::MOUSE_MOVE_ABS_PACKET{.x = boost::endian::native_to_big((short)10),
                                                     .y = boost::endian::native_to_big((short)20),
                                                     .width = boost::endian::native_to_big((short)1920),
                                                     .height = boost::endian::native_to_big((short)1080)};
  mouse::move_mouse_abs(touch_el.get(), mv_packet);

  events = fetch_events(mouse_abs);
  REQUIRE(events.size() == 2);
  REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_X"));
  REQUIRE(events[0]->value == 10);

  REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_ABS"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("ABS_Y"));
  REQUIRE(events[1]->value == 20);
}

TEST_CASE("uinput - joypad", "UINPUT") {
  uint8_t controller_caps = data::ANALOG_TRIGGERS;
  libevdev_ptr controller_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr controller_el = {
      controller::create_controller(controller_dev.get(), data::PS, controller_caps).value(),
      ::libevdev_uinput_destroy};

  link_devnode(controller_dev.get(), controller_el.get());

  auto events = fetch_events(controller_dev);
  REQUIRE(events.empty());

  auto prev_packet = data::pkts::CONTROLLER_MULTI_PACKET{};
  auto mv_packet = data::pkts::CONTROLLER_MULTI_PACKET{.controller_number = 0, .button_flags = data::RIGHT_STICK};
  controller::controller_handle(controller_el.get(), mv_packet, prev_packet);

  events = fetch_events(controller_dev);
  REQUIRE(events.size() == 1);
  REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_KEY"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("BTN_THUMBR"));
  REQUIRE(events[0]->value == 1);
}

TEST_CASE("uinput - paste UTF8", "UINPUT") {

  SECTION("UTF8 to HEX") {
    auto utf8 = boost::locale::conv::to_utf<wchar_t>("\xF0\x9F\x92\xA9", "UTF-8"); // UTF-8 '💩'
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf8);
    REQUIRE_THAT(keyboard::to_hex(utf32), Equals("1F4A9"));
  }

  SECTION("UTF16 to HEX") {
    char16_t payload[] = {0xD83D, 0xDCA9}; // UTF-16 '💩'
    auto utf16 = std::u16string(payload, 2);
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf16);
    REQUIRE_THAT(keyboard::to_hex(utf32), Equals("1F4A9"));
  }

  SECTION("Paste UTF8") {
    libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
    libevdev_uinput_ptr keyboard_el = {keyboard::create_keyboard(keyboard_dev.get()).value(),
                                       ::libevdev_uinput_destroy};

    link_devnode(keyboard_dev.get(), keyboard_el.get());

    auto events = fetch_events(keyboard_dev);
    REQUIRE(events.empty());

    auto utf8_pkt = data::pkts::UTF8_TEXT_PACKET{.text = "\xF0\x9F\x92\xA9"};
    utf8_pkt.data_size = boost::endian::native_to_big(8);

    keyboard::paste_utf(keyboard_el.get(), utf8_pkt);

    events = fetch_events(keyboard_dev);
    REQUIRE(events.size() == 16);

    /**
     * Lambda, checks that the given key_name has been correctly sent via evdev
     */
    auto ev_idx = 0;
    auto require_ev = [&](const std::string &key_name, bool pressed = true) {
      REQUIRE_THAT(libevdev_event_code_get_name(events[ev_idx]->type, events[ev_idx]->code), Equals(key_name));
      REQUIRE(events[ev_idx]->value == (pressed ? 1 : 0));
      ev_idx++;
    };

    /*
     * Pressing <CTRL> + <SHIFT> + U
     */
    require_ev("KEY_LEFTCTRL");
    require_ev("KEY_LEFTSHIFT");
    require_ev("KEY_U");
    require_ev("KEY_U", false); // release U

    /*
     * At this point we should have typed: U+1F4A9
     * (twice each because it's <press>, <release>
     */
    require_ev("KEY_1");
    require_ev("KEY_1", false);
    require_ev("KEY_F");
    require_ev("KEY_F", false);
    require_ev("KEY_4");
    require_ev("KEY_4", false);
    require_ev("KEY_A");
    require_ev("KEY_A", false);
    require_ev("KEY_9");
    require_ev("KEY_9", false);

    /*
     * Finally, releasing <CTRL> and <SHIFT>
     */
    require_ev("KEY_LEFTSHIFT", false);
    require_ev("KEY_LEFTCTRL", false);
  }
}
