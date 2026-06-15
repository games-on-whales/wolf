#include "wayland-display.hpp"
#include "wayland-client.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_contains.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <control/input_handler.hpp>
#include <core/virtual-display.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;
using Catch::Matchers::SizeIs;
using Catch::Matchers::StartsWith;

using namespace wolf::core;
using namespace wolf::core::virtual_display;
using namespace moonlight::control;

TEST_CASE("Wayland virtual inputs", "[WAYLAND]") {
  const auto FPS = 60;

  TestWaylandDisplay display({.width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .refreshRate = FPS});

  auto session = events::StreamSession{
      .mouse = std::make_shared<std::optional<events::MouseTypes>>(WaylandMouse(display.w_state)),
      .keyboard = std::make_shared<std::optional<events::KeyboardTypes>>(WaylandKeyboard(display.w_state))};

  auto display_name = get_wayland_socket_name(*display.w_state);
  auto wd = w_connect(display_name);
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
    // Values come from zwp_relative_pointer_v1 as wl_fixed_t.
    // Smithay encodes the delta with an internal 256x scale, so 1 pixel = 65536 raw units.
    REQUIRE(m_ev.value().x == 10 * 65536);
    REQUIRE(m_ev.value().y == 20 * 65536);

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
    wl_display_roundtrip(wd.get());

    // Compositor sends: axis_source, axis, axis_value120, axis_relative_direction (v9), frame.
    // axis_relative_direction is silently dropped (no queue entry) since we only log it.
    auto s_ev = mouse_events_q->pop();
    REQUIRE(s_ev.has_value());
    REQUIRE(s_ev.value().type == MouseEventType::AXIS_SOURCE);

    s_ev = mouse_events_q->pop();
    REQUIRE(s_ev.has_value());
    REQUIRE((s_ev.value().type == MouseEventType::AXIS_VALUE120 || s_ev.value().type == MouseEventType::AXIS));

    s_ev = mouse_events_q->pop();
    REQUIRE(s_ev.has_value());
    REQUIRE((s_ev.value().type == MouseEventType::AXIS_VALUE120 || s_ev.value().type == MouseEventType::AXIS));

    s_ev = mouse_events_q->pop();
    REQUIRE(s_ev.has_value());
    REQUIRE(s_ev.value().type == MouseEventType::FRAME);
  }
}
