#include "libinput.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <control/input_handler.hpp>
#include <platforms/input.hpp>
#include <platforms/linux/uinput/uinput.hpp>
#include <runners/docker.hpp>
#include <sessions/handlers.hpp>
#include <state/config.hpp>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::Equals;
using Catch::Matchers::StartsWith;

using namespace wolf::core::input;
using namespace wolf::core;
using namespace moonlight::control;
using namespace std::string_literals;

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
      REQUIRE_THAT(event["DEVPATH"], StartsWith("/devices/virtual/misc/uhid/0005:054C"));
      if (event["SUBSYSTEM"] == "input") {
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/input/"));
      } else if (event["SUBSYSTEM"] == "hidraw") {
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/hidraw"));
      }
    }
  }
}

TEST_CASE("Plug a PS5 pad with its hidraw node before its input nodes", "[UHID]") {
  events::App app = {};
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                       .app = std::make_shared<events::App>(app)};
  auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{.controller_number = 0,
                                               .controller_type = pkts::PS,
                                               .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::TOUCHPAD};
  c_pkt.type = pkts::CONTROLLER_ARRIVAL;
  control::handle_input(session, {}, &c_pkt);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto udev_events = std::get<PS5Joypad>(*session.joypads->load()->at(0)).get_udev_events();
  auto plugged = docker::hidraw_events_first(udev_events);

  REQUIRE(plugged.size() == udev_events.size());
  REQUIRE_THAT(plugged.front()["SUBSYSTEM"], Equals("hidraw"));
  REQUIRE_THAT(plugged.front()["DEVNAME"], StartsWith("/dev/hidraw"));
}

TEST_CASE("fake-udev command carries the subsystem of the event", "[UHID]") {
  events::App app = {};
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                       .app = std::make_shared<events::App>(app)};
  auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{.controller_number = 0,
                                               .controller_type = pkts::PS,
                                               .capabilities = pkts::ANALOG_TRIGGERS | pkts::RUMBLE | pkts::TOUCHPAD};
  c_pkt.type = pkts::CONTROLLER_ARRIVAL;
  control::handle_input(session, {}, &c_pkt);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto udev_events =
      docker::hidraw_events_first(std::get<PS5Joypad>(*session.joypads->load()->at(0)).get_udev_events());

  REQUIRE_THAT(docker::fake_udev_command(udev_events.front()), EndsWith("--udev-subsystem hidraw"));
  REQUIRE_THAT(docker::fake_udev_command(udev_events.back()), EndsWith("--udev-subsystem input"));
}

TEST_CASE("Joining a lobby plugs all joypads in one event", "[UHID]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .lobbies = std::make_shared<immer::atom<immer::vector<events::Lobby>>>(),
      .running_sessions = running_sessions});

  events::Lobby lobby{.id = "lobby-under-test",
                      .name = "test_lobby",
                      .started_by_profile_id = "test_profile",
                      .multi_user = false,
                      .stop_when_everyone_leaves = false};
  app_state->lobbies->update([&](const immer::vector<events::Lobby> &lobbies) { return lobbies.push_back(lobby); });

  // A session with an Xbox pad and a DualSense
  auto session = events::StreamSession{.event_bus = event_bus,
                                       .app = std::make_shared<events::App>(events::App{}),
                                       .session_id = 1234};
  for (auto [controller_number, type] : {std::pair{0, pkts::XBOX}, std::pair{1, pkts::PS}}) {
    auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{.controller_number = (uint8_t)controller_number,
                                                 .controller_type = type,
                                                 .capabilities = pkts::ANALOG_TRIGGERS};
    c_pkt.type = pkts::CONTROLLER_ARRIVAL;
    control::handle_input(session, {}, &c_pkt);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  REQUIRE(session.joypads->load()->size() == 2);
  running_sessions->update([&](const auto &sessions) { return sessions.push_back(session); });

  std::size_t expected_events = 0;
  for (auto [_controller_number, joypad] : *session.joypads->load()) {
    std::visit([&](auto &pad) { expected_events += pad.get_udev_events().size(); }, *joypad);
  }

  auto lobbies_handlers = sessions::setup_lobbies_handlers(app_state, "/tmp", {});
  event_bus->fire_event(immer::box<events::JoinLobbyEvent>{
      events::JoinLobbyEvent{.lobby_id = lobby.id, .moonlight_session_id = session.session_id}});

  auto plug_ev = lobby.plugged_devices_queue->pop(std::chrono::milliseconds(100));
  REQUIRE(plug_ev);
  REQUIRE_FALSE(lobby.plugged_devices_queue->pop(std::chrono::milliseconds(100)));

  auto plugged = docker::hidraw_events_first(plug_ev->get().udev_events);
  REQUIRE(plugged.size() == expected_events);
  REQUIRE_THAT(plugged.front()["SUBSYSTEM"], Equals("hidraw"));
  for (auto &udev_ev : plugged | ranges::views::drop(1)) {
    REQUIRE_THAT(udev_ev["DEVNAME"], StartsWith("/dev/input/"));
  }
}
