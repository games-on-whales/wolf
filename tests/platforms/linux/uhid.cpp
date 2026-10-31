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

namespace {
std::string uppercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return std::toupper(ch); });
  return value;
}

std::string switch_joystick_devnode(const std::shared_ptr<events::JoypadTypes> &joypad) {
  std::vector<std::map<std::string, std::string>> udev_events = joypad->get_udev_events();

  auto it = std::find_if(udev_events.begin(), udev_events.end(), [](const auto &event) {
    return event.contains("ID_INPUT_JOYSTICK") && event.contains("DEVNAME");
  });
  REQUIRE(it != udev_events.end());
  return it->at("DEVNAME");
}

struct AxisExtremes {
  int min_rx = 0;
  int max_rx = 0;
  int min_ry = 0;
  int max_ry = 0;
  bool seen_rx = false;
  bool seen_ry = false;
};

void apply_and_collect(events::StreamSession &session,
                       libevdev_ptr &dev,
                       const std::vector<std::pair<short, short>> &samples,
                       AxisExtremes &extremes) {
  for (const auto &[rx, ry] : samples) {
    auto pkt = pkts::CONTROLLER_MULTI_PACKET{
        .controller_number = 0,
        .active_gamepad_mask = 1,
        .right_stick_x = rx,
        .right_stick_y = ry,
    };
    pkt.type = pkts::CONTROLLER_MULTI;
    control::handle_input(session, {}, &pkt);

    auto events = fetch_events_debug(dev);
    for (const auto &event : events) {
      if (event->type != EV_ABS) {
        continue;
      }
      if (event->code == ABS_RX) {
        if (!extremes.seen_rx) {
          extremes.min_rx = extremes.max_rx = event->value;
          extremes.seen_rx = true;
        } else {
          extremes.min_rx = std::min(extremes.min_rx, static_cast<int>(event->value));
          extremes.max_rx = std::max(extremes.max_rx, static_cast<int>(event->value));
        }
      } else if (event->code == ABS_RY) {
        if (!extremes.seen_ry) {
          extremes.min_ry = extremes.max_ry = event->value;
          extremes.seen_ry = true;
        } else {
          extremes.min_ry = std::min(extremes.min_ry, static_cast<int>(event->value));
          extremes.max_ry = std::max(extremes.max_ry, static_cast<int>(event->value));
        }
      }
    }
  }
}
} // namespace

TEST_CASE("Create PS5 pad with CONTROLLER_ARRIVAL", "[UHID]") {
  events::App app = {};
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                       .app = std::make_shared<events::App>(app)};
  uint8_t controller_number = 1;
  auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{
      .controller_number = controller_number,
      .controller_type = pkts::PS,
      .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::TOUCHPAD | pkts::GYRO};
  c_pkt.type = pkts::CONTROLLER_ARRIVAL;

  control::handle_input(session, {}, &c_pkt);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto joypad = session.joypads->load()->at(controller_number);
  std::vector<std::string> dev_nodes;
  dev_nodes = joypad->get_nodes();
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
    REQUIRE(events.size() >= 5);
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
    udev_events = joypad->get_udev_events();

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
      REQUIRE_THAT(event["DEVPATH"], StartsWith("/devices/virtual/misc/uhid/0005:054C"));
      if (event["SUBSYSTEM"] == "input") {
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/input/"));
      } else if (event["SUBSYSTEM"] == "hidraw") {
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/hidraw"));
      }
    }
  }
}

TEST_CASE("Create Switch pad with consistent udev identity", "[UHID]") {
  events::App app = {};
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                       .app = std::make_shared<events::App>(app)};
  uint8_t controller_number = 1;
  auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{
      .controller_number = controller_number,
      .controller_type = pkts::NINTENDO,
      .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::GYRO | pkts::ACCELEROMETER};
  c_pkt.type = pkts::CONTROLLER_ARRIVAL;

  control::handle_input(session, {}, &c_pkt);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto joypad = session.joypads->load()->at(controller_number);
  std::vector<std::map<std::string, std::string>> udev_events;
  std::vector<std::pair<std::string, std::vector<std::string>>> hwdb_entries;
  std::string uniq;
  // The pad is created through inputtino::create_joypad(); on a uhid host that is
  // a rich SwitchJoypad, whose MAC seeds the udev identity below.
  if (auto *sw = dynamic_cast<inputtino::SwitchJoypad *>(joypad.get())) {
    uniq = sw->get_mac_address();
  }
  udev_events = joypad->get_udev_events();
  hwdb_entries = joypad->get_udev_hw_db_entries();

  REQUIRE(session.joypads->load()->size() == 1);
  REQUIRE(udev_events.size() == 3);
  REQUIRE(hwdb_entries.size() == 3);

  const auto is_switch_hid = [](const auto &event) {
    auto it = event.find("SUBSYSTEM");
    return it != event.end() && it->second == "hidraw";
  };
  const auto has_key_value = [](const auto &rows, std::string_view expected) {
    return std::find(rows.begin(), rows.end(), expected) != rows.end();
  };
  const auto has_key_value_exact = [](const auto &rows, std::string_view prefix, std::string expected_value) {
    auto it = std::find_if(rows.begin(), rows.end(), [&](const std::string &row) {
      return row.rfind(prefix, 0) == 0 && row.substr(prefix.size()) == expected_value;
    });
    return it != rows.end();
  };
  const auto canonical_uniq = uppercase(uniq);

  auto hidraw_event =
      std::find_if(udev_events.begin(), udev_events.end(), [&](const auto &event) { return is_switch_hid(event); });
  REQUIRE(hidraw_event != udev_events.end());
  REQUIRE_THAT(hidraw_event->at("DEVNAME"), ContainsSubstring("/dev/hidraw"));
  REQUIRE_THAT(hidraw_event->at("HID_NAME"), Equals("Pro Controller"));
  REQUIRE_THAT(hidraw_event->at("HID_UNIQ"), Equals(canonical_uniq));
  REQUIRE_THAT(hidraw_event->at("HID_ID"), Equals("0005:0000057E:00002009"));
  REQUIRE_THAT(hidraw_event->at("ID_VENDOR_ID"), Equals("057e"));
  REQUIRE_THAT(hidraw_event->at("ID_MODEL_ID"), Equals("2009"));
  REQUIRE_THAT(hidraw_event->at("ID_BUS"), Equals("bluetooth"));
  REQUIRE_THAT(hidraw_event->at("MODALIAS"), Equals("hid:b0005g0001v0000057Ep00002009"));
  REQUIRE(hidraw_event->contains("HID_PHYS"));

  std::size_t joystick_events = 0;
  std::size_t imu_events = 0;
  for (const auto &event : udev_events) {
    if (is_switch_hid(event)) {
      continue;
    }

    REQUIRE_THAT(event.at("UNIQ"), Equals(canonical_uniq));
    REQUIRE_THAT(event.at("HID_UNIQ"), Equals(canonical_uniq));
    REQUIRE_THAT(event.at("HID_NAME"), Equals("Pro Controller"));
    REQUIRE_THAT(event.at("HID_ID"), Equals("0005:0000057E:00002009"));
    REQUIRE_THAT(event.at("ID_VENDOR_ID"), Equals("057e"));
    REQUIRE_THAT(event.at("ID_MODEL_ID"), Equals("2009"));
    REQUIRE_THAT(event.at("ID_BUS"), Equals("bluetooth"));
    REQUIRE_THAT(event.at("MODALIAS"), StartsWith("input:b0005v057Ep2009"));
    REQUIRE(event.contains("PHYS"));

    if (event.find("ID_INPUT_JOYSTICK") != event.end()) {
      ++joystick_events;
      REQUIRE_THAT(event.at("DEVNAME"), ContainsSubstring("/dev/input/event"));
    }
    if (event.find("ID_INPUT_ACCELEROMETER") != event.end()) {
      ++imu_events;
      REQUIRE_THAT(event.at("DEVNAME"), ContainsSubstring("/dev/input/event"));
    }
  }

  REQUIRE(joystick_events == 1);
  REQUIRE(imu_events == 1);

  std::size_t hidraw_hwdb = 0;
  std::size_t joystick_hwdb = 0;
  std::size_t imu_hwdb = 0;
  for (const auto &[filename, rows] : hwdb_entries) {
    REQUIRE(has_key_value_exact(rows, "E:HID_UNIQ=", canonical_uniq));
    REQUIRE(has_key_value(rows, "E:HID_NAME=Pro Controller"));
    REQUIRE(has_key_value(rows, "E:HID_ID=0005:0000057E:00002009"));
    REQUIRE(has_key_value(rows, "E:ID_VENDOR_ID=057e"));
    REQUIRE(has_key_value(rows, "E:ID_MODEL_ID=2009"));
    REQUIRE(has_key_value(rows, "E:ID_BUS=bluetooth"));
    if (has_key_value(rows, "E:SUBSYSTEM=hidraw")) {
      ++hidraw_hwdb;
      REQUIRE_THAT(filename, StartsWith("c239:"));
      REQUIRE(has_key_value(rows, "E:HID_PHYS=bluetooth"));
      REQUIRE(has_key_value(rows, "E:MODALIAS=hid:b0005g0001v0000057Ep00002009"));
    } else if (has_key_value(rows, "E:ID_INPUT_ACCELEROMETER=1")) {
      ++imu_hwdb;
      REQUIRE_THAT(filename, StartsWith("c13:"));
      REQUIRE(has_key_value(rows, "E:PHYS=bluetooth"));
      REQUIRE_THAT(*std::find_if(rows.begin(),
                                 rows.end(),
                                 [](const std::string &row) { return row.rfind("E:MODALIAS=", 0) == 0; }),
                   StartsWith("E:MODALIAS=input:b0005v057Ep2009"));
    } else if (has_key_value(rows, "E:ID_INPUT_JOYSTICK=1")) {
      ++joystick_hwdb;
      REQUIRE_THAT(filename, StartsWith("c13:"));
      REQUIRE(has_key_value(rows, "E:PHYS=bluetooth"));
      REQUIRE_THAT(*std::find_if(rows.begin(),
                                 rows.end(),
                                 [](const std::string &row) { return row.rfind("E:MODALIAS=", 0) == 0; }),
                   StartsWith("E:MODALIAS=input:b0005v057Ep2009"));
    }
  }

  REQUIRE(hidraw_hwdb == 1);
  REQUIRE(joystick_hwdb == 1);
  REQUIRE(imu_hwdb == 1);
}

TEST_CASE("Switch reconnect emits consistent plug and unplug metadata", "[UHID]") {
  events::App app = {};
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                       .app = std::make_shared<events::App>(app)};

  std::vector<events::PlugDeviceEvent> plug_events;
  std::vector<events::UnplugDeviceEvent> unplug_events;
  auto plug_handler = session.event_bus->register_handler<immer::box<events::PlugDeviceEvent>>(
      [&](const immer::box<events::PlugDeviceEvent> &event) { plug_events.push_back(*event); });
  auto unplug_handler = session.event_bus->register_handler<immer::box<events::UnplugDeviceEvent>>(
      [&](const immer::box<events::UnplugDeviceEvent> &event) { unplug_events.push_back(*event); });

  const uint8_t controller_number = 1;
  auto arrival = pkts::CONTROLLER_ARRIVAL_PACKET{
      .controller_number = controller_number,
      .controller_type = pkts::NINTENDO,
      .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::GYRO | pkts::ACCELEROMETER};
  arrival.type = pkts::CONTROLLER_ARRIVAL;

  // Snapshot of the lifecycle seen from Moonlight in practice:
  // controller arrives, the final CONTROLLER_MULTI clears the active bit on
  // disconnect, then the same controller arrives again on reconnect.
  control::handle_input(session, {}, &arrival);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto disconnect = pkts::CONTROLLER_MULTI_PACKET{
      .controller_number = controller_number,
      .active_gamepad_mask = 0,
  };
  disconnect.type = pkts::CONTROLLER_MULTI;
  control::handle_input(session, {}, &disconnect);

  control::handle_input(session, {}, &arrival);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  REQUIRE(plug_events.size() == 2);
  REQUIRE(unplug_events.size() == 1);
  REQUIRE(session.joypads->load()->size() == 1);

  const auto classify = [](const auto &udev_events) {
    std::size_t hidraw = 0;
    std::size_t joystick = 0;
    std::size_t imu = 0;
    std::string uniq;
    std::string hid_uniq;

    for (const auto &event : udev_events) {
      if (auto it = event.find("HID_UNIQ"); it != event.end()) {
        hid_uniq = it->second;
      }
      if (auto it = event.find("UNIQ"); it != event.end()) {
        uniq = it->second;
      }

      if (auto it = event.find("SUBSYSTEM"); it != event.end() && it->second == "hidraw") {
        ++hidraw;
      }
      if (event.find("ID_INPUT_JOYSTICK") != event.end()) {
        ++joystick;
      }
      if (event.find("ID_INPUT_ACCELEROMETER") != event.end()) {
        ++imu;
      }
    }

    return std::tuple{hidraw, joystick, imu, uniq, hid_uniq};
  };

  const auto [first_hidraw, first_joystick, first_imu, first_uniq, first_hid_uniq] =
      classify(plug_events[0].udev_events);
  const auto [unplug_hidraw, unplug_joystick, unplug_imu, unplug_uniq, unplug_hid_uniq] =
      classify(unplug_events[0].udev_events);
  const auto [second_hidraw, second_joystick, second_imu, second_uniq, second_hid_uniq] =
      classify(plug_events[1].udev_events);

  REQUIRE(first_hidraw == 1);
  REQUIRE(first_joystick == 1);
  REQUIRE(first_imu == 1);
  REQUIRE(unplug_hidraw == 1);
  REQUIRE(unplug_joystick == 1);
  REQUIRE(unplug_imu == 1);
  REQUIRE(second_hidraw == 1);
  REQUIRE(second_joystick == 1);
  REQUIRE(second_imu == 1);

  REQUIRE(first_uniq == unplug_uniq);
  REQUIRE(first_uniq == second_uniq);
  REQUIRE(first_hid_uniq == unplug_hid_uniq);
  REQUIRE(first_hid_uniq == second_hid_uniq);

  const auto count_subsystem = [](const auto &events, std::string_view subsystem) {
    return std::count_if(events.begin(), events.end(), [&](const auto &event) {
      auto it = event.find("SUBSYSTEM");
      return it != event.end() && it->second == subsystem;
    });
  };

  REQUIRE(count_subsystem(plug_events[0].udev_events, "hidraw") == 1);
  REQUIRE(count_subsystem(unplug_events[0].udev_events, "hidraw") == 1);
  REQUIRE(count_subsystem(plug_events[1].udev_events, "hidraw") == 1);

  (void)plug_handler;
  (void)unplug_handler;
}

TEST_CASE("Switch captured Moonlight packets preserve right stick range across reconnect", "[UHID]") {
  events::App app = {};
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                       .app = std::make_shared<events::App>(app)};

  auto arrival = pkts::CONTROLLER_ARRIVAL_PACKET{
      .controller_number = 0,
      .controller_type = pkts::NINTENDO,
      .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::GYRO | pkts::ACCELEROMETER};
  arrival.type = pkts::CONTROLLER_ARRIVAL;

  const std::vector<std::pair<short, short>> first_connect_samples = {
      {-32768, -7430},
      {-10405, 32767},
      {32767, 17561},
      {32026, -11318},
      {-32287, 5871},
  };
  const std::vector<std::pair<short, short>> second_connect_samples = {
      {-32768, -29428},
      {12356, 32767},
      {32767, 7864},
      {-30493, 7039},
      {11846, -5933},
  };

  control::handle_input(session, {}, &arrival);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto first_joypad = session.joypads->load()->at(0);
  auto first_devnode = switch_joystick_devnode(first_joypad);
  libevdev_ptr first_dev(libevdev_new(), ::libevdev_free);
  link_devnode(first_dev.get(), first_devnode);

  AxisExtremes first_extremes;
  apply_and_collect(session, first_dev, first_connect_samples, first_extremes);

  REQUIRE(first_extremes.seen_rx);
  REQUIRE(first_extremes.seen_ry);
  REQUIRE(first_extremes.min_rx < -10000);
  REQUIRE(first_extremes.max_rx > 10000);
  REQUIRE(first_extremes.min_ry < -10000);
  REQUIRE(first_extremes.max_ry > 10000);

  auto disconnect = pkts::CONTROLLER_MULTI_PACKET{
      .controller_number = 0,
      .active_gamepad_mask = 0,
  };
  disconnect.type = pkts::CONTROLLER_MULTI;
  control::handle_input(session, {}, &disconnect);

  control::handle_input(session, {}, &arrival);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto second_joypad = session.joypads->load()->at(0);
  auto second_devnode = switch_joystick_devnode(second_joypad);
  libevdev_ptr second_dev(libevdev_new(), ::libevdev_free);
  link_devnode(second_dev.get(), second_devnode);

  AxisExtremes second_extremes;
  apply_and_collect(session, second_dev, second_connect_samples, second_extremes);

  REQUIRE(second_extremes.seen_rx);
  REQUIRE(second_extremes.seen_ry);
  REQUIRE(second_extremes.min_rx < -10000);
  REQUIRE(second_extremes.max_rx > 10000);
  REQUIRE(second_extremes.min_ry < -10000);
  REQUIRE(second_extremes.max_ry > 10000);
}
