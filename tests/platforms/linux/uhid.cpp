#include "libinput.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <control/input_handler.hpp>
#include <platforms/input.hpp>
#include <platforms/linux/uinput/uinput.hpp>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using Catch::Matchers::StartsWith;

using namespace wolf::core::input;
using namespace wolf::core;
using namespace moonlight::control;
using namespace std::string_literals;

TEST_CASE("Create PS5 pad with CONTROLLER_ARRIVAL", "[UHID]") {
  events::App app = {.joypad_type = moonlight::control::pkts::CONTROLLER_TYPE::AUTO};
  auto session =
      events::StreamSession{.event_bus = std::make_shared<dp::event_bus<events::EventTypes>>(), .app = std::make_shared<events::App>(app)};
  uint8_t controller_number = 1;
  auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{
      .controller_number = controller_number,
      .controller_type = pkts::PS,
      .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::TOUCHPAD | pkts::GYRO};
  c_pkt.type = pkts::CONTROLLER_ARRIVAL;

  control::handle_input(session, {}, &c_pkt);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  auto joypad = session.joypads->load()->at(controller_number);
  std::vector<std::string> dev_nodes;
  std::visit([&dev_nodes](auto &joypad) { dev_nodes = joypad.get_nodes(); }, *joypad);
  REQUIRE(session.joypads->load()->size() == 1);
  REQUIRE(dev_nodes.size() >= 4);

  // Search dev_nodes /dev/input/eventXX device and turn them into libevdev devices
  std::sort(dev_nodes.begin(), dev_nodes.end()); // ranges::actions::sort doesn't work for some reason
  auto devices =
      dev_nodes |                                                                                              //
      ranges::views::filter([](const std::string &node) { return node.find("event") != std::string::npos; }) | //
      ranges::views::transform([](const std::string &node) {
        libevdev_ptr el(libevdev_new(), ::libevdev_free);
        link_devnode(el.get(), node);
        return el;
      }) |
      ranges::to_vector;

  // We know the 3rd device is the touchpad
  auto touch_rel_dev = devices[2];
  {   // "Joypad touchpad"
    { // Touch finger one
      auto touch_packet = pkts::CONTROLLER_TOUCH_PACKET{.controller_number = controller_number,
                                                        .event_type = moonlight::control::pkts::TOUCH_EVENT_DOWN,
                                                        .pointer_id = 0,
                                                        .x = {255, 255, 255, 0},
                                                        .y = {0, 255, 255, 255}};
      touch_packet.type = pkts::CONTROLLER_TOUCH;

      control::handle_input(session, {}, &touch_packet);
      auto events = fetch_events_debug(touch_rel_dev);
      REQUIRE(events.size() == 3);

      REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_MT_TRACKING_ID"));
      REQUIRE(events[0]->value == 0);

      REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("BTN_TOUCH"));
      REQUIRE(events[1]->value == 1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[2]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[2]->type, events[2]->code), Equals("BTN_TOOL_FINGER"));
      REQUIRE(events[2]->value == 1);
    }

    { // Touch finger 2
      auto touch_2_pkt = pkts::CONTROLLER_TOUCH_PACKET{.controller_number = controller_number,
                                                       .event_type = moonlight::control::pkts::TOUCH_EVENT_DOWN,
                                                       .pointer_id = boost::endian::native_to_little(1),
                                                       .x = {255, 255, 255, 0},
                                                       .y = {0, 255, 255, 255}};
      touch_2_pkt.type = pkts::CONTROLLER_TOUCH;

      control::handle_input(session, {}, &touch_2_pkt);
      auto events = fetch_events_debug(touch_rel_dev);
      REQUIRE(events.size() == 4);

      REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_MT_SLOT"));
      REQUIRE(events[0]->value == 1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("ABS_MT_TRACKING_ID"));
      REQUIRE(events[1]->value == 1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[2]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[2]->type, events[2]->code), Equals("BTN_TOOL_FINGER"));
      REQUIRE(events[2]->value == 0);

      REQUIRE_THAT(libevdev_event_type_get_name(events[3]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[3]->type, events[3]->code), Equals("BTN_TOOL_DOUBLETAP"));
      REQUIRE(events[3]->value == 1);
    }

    { // Remove finger one
      auto touch_2_pkt = pkts::CONTROLLER_TOUCH_PACKET{.controller_number = controller_number,
                                                       .event_type = moonlight::control::pkts::TOUCH_EVENT_UP,
                                                       .pointer_id = 0,
                                                       .x = {0},
                                                       .y = {0}};
      touch_2_pkt.type = pkts::CONTROLLER_TOUCH;

      control::handle_input(session, {}, &touch_2_pkt);
      auto events = fetch_events_debug(touch_rel_dev);
      REQUIRE(events.size() == 4);

      REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_MT_SLOT"));
      REQUIRE(events[0]->value == 0);

      REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("ABS_MT_TRACKING_ID"));
      REQUIRE(events[1]->value == -1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[2]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[2]->type, events[2]->code), Equals("BTN_TOOL_FINGER"));
      REQUIRE(events[2]->value == 1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[3]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[3]->type, events[3]->code), Equals("BTN_TOOL_DOUBLETAP"));
      REQUIRE(events[3]->value == 0);
    }

    { // Remove finger two, no fingers left on the touchpad
      auto touch_2_pkt = pkts::CONTROLLER_TOUCH_PACKET{.controller_number = controller_number,
                                                       .event_type = moonlight::control::pkts::TOUCH_EVENT_UP,
                                                       .pointer_id = boost::endian::native_to_little(1),
                                                       .x = {0},
                                                       .y = {0}};
      touch_2_pkt.type = pkts::CONTROLLER_TOUCH;

      control::handle_input(session, {}, &touch_2_pkt);
      auto events = fetch_events_debug(touch_rel_dev);
      REQUIRE(events.size() == 4); // TODO: why there are no ABS_X and ABS_Y?

      REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_MT_SLOT"));
      REQUIRE(events[0]->value == 1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_ABS"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("ABS_MT_TRACKING_ID"));
      REQUIRE(events[1]->value == -1);

      REQUIRE_THAT(libevdev_event_type_get_name(events[2]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[2]->type, events[2]->code), Equals("BTN_TOUCH"));
      REQUIRE(events[2]->value == 0);

      REQUIRE_THAT(libevdev_event_type_get_name(events[3]->type), Equals("EV_KEY"));
      REQUIRE_THAT(libevdev_event_code_get_name(events[3]->type, events[3]->code), Equals("BTN_TOOL_FINGER"));
      REQUIRE(events[3]->value == 0);
    }
  }

  // We know the 2nd device is the motion sensor
  auto motion_dev = devices[1];
  { // Motion sensor
    auto motion_pkt = pkts::CONTROLLER_MOTION_PACKET{.controller_number = controller_number,
                                                     .motion_type = pkts::ACCELERATION,
                                                     .x = {255, 255, 255, 0},
                                                     .y = {0, 255, 255, 255},
                                                     .z = {0, 0, 0, 0}};
    motion_pkt.type = pkts::CONTROLLER_MOTION;

    control::handle_input(session, {}, &motion_pkt);
    auto events = fetch_events_debug(motion_dev);
    REQUIRE(events.size() == 5);
    // TODO: seems that I only get MSC_TIMESTAMP here
    //
    //      REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
    //      REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_X"));
    //      REQUIRE(events[0]->value == 0);
    //
    //      REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_ABS"));
    //      REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("ABS_Y"));
    //      REQUIRE(events[1]->value == -32768); // DS_ACC_RANGE
    //
    //      REQUIRE_THAT(libevdev_event_type_get_name(events[2]->type), Equals("EV_ABS"));
    //      REQUIRE_THAT(libevdev_event_code_get_name(events[2]->type, events[2]->code), Equals("ABS_Z"));
    //      REQUIRE(events[2]->value == 0);
    //
    //      REQUIRE_THAT(libevdev_event_type_get_name(events[3]->type), Equals("EV_MSC"));
    //      REQUIRE_THAT(libevdev_event_code_get_name(events[3]->type, events[3]->code), Equals("MSC_TIMESTAMP"));
  }

  { // UDEV
    std::vector<std::map<std::string, std::string>> udev_events;
    std::visit([&udev_events](auto &joypad) { udev_events = joypad.get_udev_events(); }, *joypad);

    for (auto event : udev_events) {
      std::stringstream ss;
      for (auto [key, value] : event) {
        ss << key << "=" << value << ", ";
      }
      logs::log(logs::debug, "UDEV: {}", ss.str());
    }

    REQUIRE(udev_events.size() == 7);

    for (auto &event : udev_events) {
      REQUIRE_THAT(event["ACTION"], Equals("add"));
      REQUIRE_THAT(event["DEVPATH"], StartsWith("/devices/virtual/misc/uhid/0003:054C"));
      if (event["SUBSYSTEM"] == "input") {
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/input/"));
      } else if (event["SUBSYSTEM"] == "hidraw") {
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/hidraw"));
      }
    }
  }
}