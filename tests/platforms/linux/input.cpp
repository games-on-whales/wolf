#include "catch2/catch_all.hpp"
#include <boost/endian/conversion.hpp>
#include <chrono>
#include <fcntl.h>
#include <platforms/linux/keyboard.hpp>
#include <platforms/linux/uinput.hpp>
#include <thread>

using Catch::Matchers::Equals;

using namespace input;

void link_devnode(libevdev *dev, libevdev_uinput *dev_input) {
  // We have to sleep in order to be able to read from the newly created device
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto fd = open(libevdev_uinput_get_devnode(dev_input), O_RDONLY | O_NONBLOCK);
  REQUIRE(fd >= 0);
  libevdev_set_fd(dev, fd);
}

TEST_CASE("Keyboard support", "UINPUT") {
  libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
  libevdev_uinput_ptr keyboard_el = {keyboard::create_keyboard(keyboard_dev.get()).value(), ::libevdev_uinput_destroy};
  struct input_event ev {};

  link_devnode(keyboard_dev.get(), keyboard_el.get());

  auto rc = libevdev_next_event(keyboard_dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev);
  REQUIRE(rc == -EAGAIN);

  auto press_b_key = data::KEYBOARD_PACKET{.key_action = 0, .key_code = boost::endian::native_to_big((short)0x42)};
  keyboard::keyboard_handle(keyboard_el.get(), press_b_key);

  rc = libevdev_next_event(keyboard_dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev);
  REQUIRE(rc == LIBEVDEV_READ_STATUS_SUCCESS);
  REQUIRE_THAT(libevdev_event_type_get_name(ev.type), Equals("EV_KEY"));
  REQUIRE_THAT(libevdev_event_code_get_name(ev.type, ev.code), Equals("KEY_B"));
}