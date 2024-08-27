#include "wayland-client.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_contains.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <control/input_handler.hpp>
#include <core/virtual-display.hpp>
#include <csignal>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;
using Catch::Matchers::SizeIs;

using namespace wolf::core;
using namespace wolf::core::virtual_display;
using namespace moonlight::control;

TEST_CASE("Wayland C APIs", "[WAYLAND]") {
  auto w_state = create_wayland_display({});

  auto env_vars = get_env(*w_state);
  REQUIRE_THAT(env_vars, SizeIs(1));
  REQUIRE_THAT(env_vars, Contains("WAYLAND_DISPLAY=wayland-1"));

  auto graphic_devices = get_devices(*w_state);
  REQUIRE_THAT(graphic_devices, SizeIs(2));
  REQUIRE_THAT(graphic_devices, Contains("/dev/dri/renderD128"));
  REQUIRE_THAT(graphic_devices, Contains("/dev/dri/card0"));

  { // Set resolution to 1080p
    auto caps = set_resolution(*w_state, {1920, 1080, 60});

    auto gst_buffer = get_frame(*w_state);
    REQUIRE(GST_IS_BUFFER(gst_buffer));
    REQUIRE(gst_buffer_get_size(gst_buffer) == 1920 * 1080 * 4);
    REQUIRE_THAT(
        gst_caps_to_string(caps.get()),
        Equals("video/x-raw, width=(int)1920, height=(int)1080, framerate=(fraction)60/1, format=(string)RGBx"));

    gst_buffer_unref(gst_buffer);
  }

  { // Set resolution to 720p
    auto caps = set_resolution(*w_state, {1280, 720, 30});

    auto gst_buffer = get_frame(*w_state);
    REQUIRE(GST_IS_BUFFER(gst_buffer));
    REQUIRE(gst_buffer_get_size(gst_buffer) == 1280 * 720 * 4);
    REQUIRE_THAT(
        gst_caps_to_string(caps.get()),
        Equals("video/x-raw, width=(int)1280, height=(int)720, framerate=(fraction)30/1, format=(string)RGBx"));

    gst_buffer_unref(gst_buffer);
  }
}

TEST_CASE("Wayland virtual inputs", "[WAYLAND]") {
  auto w_state = create_wayland_display({});
  const auto FPS = 60;
  set_resolution(*w_state, {WINDOW_WIDTH, WINDOW_HEIGHT, FPS});
  auto mouse = wolf::core::virtual_display::WaylandMouse(w_state);
  auto keyboard = wolf::core::virtual_display::WaylandKeyboard(w_state);
  auto session = events::StreamSession{.mouse = std::make_shared<std::optional<events::MouseTypes>>(mouse),
                                       .keyboard = std::make_shared<std::optional<events::KeyboardTypes>>(keyboard)};

  auto wd = w_connect(w_state);
  auto w_objects = w_get_state(wd);

  w_display_create_window(*w_objects);
  wl_display_roundtrip(wd.get());

  auto mouse_events_q = w_get_mouse_queue(*w_objects);
  auto kb_events_q = w_get_keyboard_queue(*w_objects);
  wl_display_roundtrip(wd.get());

  { // simulate the window being displayed
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
    commit_frame(w_objects.get());
    wl_display_roundtrip(wd.get());
  }

  { // First move the mouse, this way our virtual window will get into focus
    auto mv_packet = pkts::MOUSE_MOVE_REL_PACKET{.delta_x = 10, .delta_y = 20};
    mv_packet.type = pkts::MOUSE_MOVE_REL;
    control::handle_input(session, {}, &mv_packet);
    wl_display_roundtrip(wd.get());

    auto m_ev = mouse_events_q->pop();
    REQUIRE(m_ev.has_value());
    REQUIRE(m_ev.value().type == MouseEventType::ENTER);

    m_ev = mouse_events_q->pop();
    REQUIRE(m_ev.has_value());
    REQUIRE(m_ev.value().type == MouseEventType::MOTION);
    // TODO: why are dx=655360, dy=1310720 ???

    m_ev = mouse_events_q->pop();
    REQUIRE(m_ev.has_value());
    REQUIRE(m_ev.value().type == MouseEventType::FRAME);
  }

  // Keyboard tests
  {
    auto press_A_key = pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0x41)};
    press_A_key.type = pkts::KEY_PRESS;
    control::handle_input(session, {}, &press_A_key);
    wl_display_roundtrip(wd.get());

    auto k_ev = kb_events_q->pop();
    REQUIRE(k_ev.has_value());
    REQUIRE(k_ev->keycode == 30);
    REQUIRE(k_ev->pressed);
  }

  {
    auto release_A_key = pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0x41)};
    release_A_key.type = pkts::KEY_RELEASE;
    control::handle_input(session, {}, &release_A_key);
    wl_display_roundtrip(wd.get());

    auto k_ev = kb_events_q->pop();
    REQUIRE(k_ev.has_value());
    REQUIRE(k_ev->keycode == 30);
    REQUIRE(!k_ev->pressed);
  }

  { // Testing modifiers
    auto press_SHIFT_A =
        pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0x41), .modifiers = pkts::SHIFT};
    press_SHIFT_A.type = pkts::KEY_PRESS;
    control::handle_input(session, {}, &press_SHIFT_A);
    wl_display_roundtrip(wd.get());

    auto k_ev = kb_events_q->pop();
    // Press SHIFT
    REQUIRE(k_ev.has_value());
    REQUIRE(k_ev->keycode == 42);
    REQUIRE(k_ev->pressed);

    // Press A
    k_ev = kb_events_q->pop();
    REQUIRE(k_ev.has_value());
    REQUIRE(k_ev->keycode == 30);
    REQUIRE(k_ev->pressed);

    // Release SHIFT
    k_ev = kb_events_q->pop();
    REQUIRE(k_ev.has_value());
    REQUIRE(k_ev->keycode == 42);
    REQUIRE(!k_ev->pressed);
  }

  // Mouse tests: scroll
  {
    short scroll_amt = 10;
    auto scroll_packet = pkts::MOUSE_SCROLL_PACKET{.scroll_amt1 = boost::endian::native_to_big(scroll_amt)};
    scroll_packet.type = pkts::MOUSE_SCROLL;
    control::handle_input(session, {}, &scroll_packet);
    //    wl_display_roundtrip(wd.get());

    // TODO: seems that I don't get those events
    //       > interface 'wl_pointer' has no event 10
  }
}