#include "wayland-client.hpp"
#include <algorithm>
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
using Catch::Matchers::StartsWith;

using namespace wolf::core;
using namespace wolf::core::virtual_display;
using namespace moonlight::control;

namespace {

// Boilerplate shared by every resolution-change integration test: create display, set
// an initial mode, pump one frame to ensure the compositor's output is fully live,
// then connect a real Wayland client, bind wl_output, and create a window. Returns
// everything the test needs to keep driving the pipeline.
struct ResolutionHarness {
  std::shared_ptr<WaylandState> w_state;
  std::shared_ptr<wl_display> wd;
  std::shared_ptr<WClientState> w_objects;
};

ResolutionHarness make_resolution_harness(const DisplayMode &initial) {
  auto w_state = create_wayland_display({});
  auto caps = set_resolution(*w_state, initial);

  // Drain one frame so the compositor event loop has finished initialising the
  // Output global before any client connects.
  auto *gst_buffer = get_frame(*w_state);
  REQUIRE(GST_IS_BUFFER(gst_buffer));
  gst_buffer_unref(gst_buffer);

  auto wd = w_connect(w_state);
  auto w_objects = w_get_state(wd);
  w_display_create_window(*w_objects);
  w_roundtrip_n(wd.get(), 3);

  return {w_state, wd, w_objects};
}

} // namespace

TEST_CASE("Wayland C APIs", "[WAYLAND]") {
  auto w_state = create_wayland_display({});

  auto env_vars = get_env(*w_state);
  REQUIRE_THAT(env_vars, SizeIs(1));
  REQUIRE_THAT(env_vars, Contains(StartsWith("WAYLAND_DISPLAY=wayland-")));

  auto graphic_devices = get_devices(*w_state);
  REQUIRE_THAT(graphic_devices, SizeIs(2));
  REQUIRE_THAT(graphic_devices, Contains(StartsWith("/dev/dri/renderD")));
  REQUIRE_THAT(graphic_devices, Contains(StartsWith("/dev/dri/card")));

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

// -----------------------------------------------------------------------------
// Integration test harness: resolution-change propagation
// -----------------------------------------------------------------------------
//
// The tests below stand in the same spirit as gow#321's layered image harness: go
// past the gstreamer buffer surface (which the `Wayland C APIs` test already
// covers) and assert the behaviour a *real* Wayland client on the other end of the
// socket actually observes when Wolf changes the streamed resolution at runtime.
//
// They currently exercise a known gap: `wayland-display-core` ignores a second
// `VideoInfo` command once an output already exists (see comp/mod.rs TODO about
// automatic resolution switching). These tests document the expected contract and
// will turn green once that gap is closed.
// -----------------------------------------------------------------------------

TEST_CASE("Wayland output advertises configured mode to client", "[WAYLAND][resolution]") {
  auto h = make_resolution_harness({1920, 1080, 60});

  REQUIRE(h.w_objects->output != nullptr);
  auto events = drain_output_events(*h.w_objects->output_events);

  // The compositor is expected to emit at least one geometry, one mode and one done
  // event on bind. If versioned >= 4 it should also emit name/description.
  REQUIRE_FALSE(events.empty());

  auto mode = latest_mode(events);
  REQUIRE(mode.has_value());
  REQUIRE(mode->width == 1920);
  REQUIRE(mode->height == 1080);
  // smithay stores refresh as (1000.0 / fps) ms rounded — see comp/mod.rs.
  // We just require that *a* refresh was advertised.
  REQUIRE(mode->refresh > 0);

  auto geom = latest_geometry(events);
  REQUIRE(geom.has_value());
  REQUIRE_THAT(geom->make, Equals("Virtual"));
  REQUIRE_THAT(geom->model, Equals("Wolf"));

  auto done_count = std::count_if(events.begin(), events.end(), [](const OutputEvent &e) {
    return e.type == OUTPUT_DONE;
  });
  REQUIRE(done_count >= 1);
}

TEST_CASE("Wayland toplevel receives initial configure with window size", "[WAYLAND][resolution]") {
  auto h = make_resolution_harness({1920, 1080, 60});

  auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
  REQUIRE_FALSE(configures.empty());

  // The compositor clamps the toplevel to the intersection of the client's max_size
  // and the output size. Our test client doesn't set a max_size so it should match
  // the output.
  const auto &latest = configures.back();
  REQUIRE(latest.width == 1920);
  REQUIRE(latest.height == 1080);
}

TEST_CASE("Wayland resolution change propagates to connected client", "[WAYLAND][resolution]") {
  auto h = make_resolution_harness({1920, 1080, 60});

  // Drain everything emitted during setup so we only see new events triggered by
  // the switch.
  drain_output_events(*h.w_objects->output_events);
  drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

  { // Trigger the resolution change from Wolf's side.
    auto caps = set_resolution(*h.w_state, {1280, 720, 30});
    REQUIRE_THAT(
        gst_caps_to_string(caps.get()),
        Equals("video/x-raw, width=(int)1280, height=(int)720, framerate=(fraction)30/1, format=(string)RGBx"));
  }

  // Render one frame on the new mode to give the compositor event loop a chance to
  // flush the updated state to clients.
  {
    auto *gst_buffer = get_frame(*h.w_state);
    REQUIRE(GST_IS_BUFFER(gst_buffer));
    REQUIRE(gst_buffer_get_size(gst_buffer) == 1280 * 720 * 4);
    gst_buffer_unref(gst_buffer);
  }

  w_roundtrip_n(h.wd.get(), 3);

  auto output_events = drain_output_events(*h.w_objects->output_events);
  auto mode = latest_mode(output_events);
  REQUIRE(mode.has_value());
  REQUIRE(mode->width == 1280);
  REQUIRE(mode->height == 720);

  auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
  REQUIRE_FALSE(configures.empty());
  const auto &latest = configures.back();
  REQUIRE(latest.width == 1280);
  REQUIRE(latest.height == 720);
}

TEST_CASE("Wayland multiple resolution toggles each propagate to client", "[WAYLAND][resolution]") {
  auto h = make_resolution_harness({1920, 1080, 60});

  struct Step {
    int width;
    int height;
    int fps;
  };
  const std::vector<Step> steps = {
      {1280, 720, 60},
      {2560, 1440, 60},
      {1920, 1080, 30},
      {1280, 720, 30},
  };

  for (const auto &step : steps) {
    drain_output_events(*h.w_objects->output_events);
    drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

    set_resolution(*h.w_state, {step.width, step.height, step.fps});

    auto *gst_buffer = get_frame(*h.w_state);
    REQUIRE(GST_IS_BUFFER(gst_buffer));
    REQUIRE(gst_buffer_get_size(gst_buffer) == (size_t)step.width * step.height * 4);
    gst_buffer_unref(gst_buffer);

    w_roundtrip_n(h.wd.get(), 3);

    auto mode = latest_mode(drain_output_events(*h.w_objects->output_events));
    INFO("step = " << step.width << "x" << step.height << "@" << step.fps);
    REQUIRE(mode.has_value());
    REQUIRE(mode->width == step.width);
    REQUIRE(mode->height == step.height);

    auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
    REQUIRE_FALSE(configures.empty());
    REQUIRE(configures.back().width == step.width);
    REQUIRE(configures.back().height == step.height);
  }
}

TEST_CASE("Wayland framerate change updates output refresh for client", "[WAYLAND][resolution]") {
  auto h = make_resolution_harness({1920, 1080, 60});

  auto initial_mode = latest_mode(drain_output_events(*h.w_objects->output_events));
  REQUIRE(initial_mode.has_value());
  auto initial_refresh = initial_mode->refresh;
  REQUIRE(initial_refresh > 0);

  drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

  // Same dimensions, different framerate — only the refresh should move.
  set_resolution(*h.w_state, {1920, 1080, 30});
  auto *gst_buffer = get_frame(*h.w_state);
  REQUIRE(GST_IS_BUFFER(gst_buffer));
  gst_buffer_unref(gst_buffer);

  w_roundtrip_n(h.wd.get(), 3);

  auto mode = latest_mode(drain_output_events(*h.w_objects->output_events));
  REQUIRE(mode.has_value());
  REQUIRE(mode->width == 1920);
  REQUIRE(mode->height == 1080);
  REQUIRE(mode->refresh != initial_refresh);
}

TEST_CASE("Wayland client never sees resolution larger than current output", "[WAYLAND][resolution]") {
  // Regression guard: after a downscale, the toplevel configure size must not
  // retain stale larger dimensions from the previous mode.
  auto h = make_resolution_harness({2560, 1440, 60});

  drain_output_events(*h.w_objects->output_events);
  drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

  set_resolution(*h.w_state, {640, 480, 60});
  auto *gst_buffer = get_frame(*h.w_state);
  REQUIRE(GST_IS_BUFFER(gst_buffer));
  REQUIRE(gst_buffer_get_size(gst_buffer) == 640 * 480 * 4);
  gst_buffer_unref(gst_buffer);

  w_roundtrip_n(h.wd.get(), 3);

  auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
  REQUIRE_FALSE(configures.empty());
  for (const auto &cfg : configures) {
    INFO("configure size " << cfg.width << "x" << cfg.height);
    REQUIRE(cfg.width <= 640);
    REQUIRE(cfg.height <= 480);
  }
}

TEST_CASE("Wayland toplevel max_size clamps configure after resolution change", "[WAYLAND][resolution]") {
  // The compositor intersects each mapped toplevel's max_size with the new output
  // size before sending configure. A client that declared a max_size smaller than
  // the output should keep seeing that size across resolution changes, not get
  // resized up to the output.
  auto h = make_resolution_harness({1280, 720, 60});

  xdg_toplevel_set_max_size(h.w_objects->toplevel.get(), 800, 600);
  wl_surface_commit(h.w_objects->surface.get());
  w_roundtrip_n(h.wd.get(), 3);

  drain_output_events(*h.w_objects->output_events);
  drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

  set_resolution(*h.w_state, {1920, 1080, 60});
  auto *gst_buffer = get_frame(*h.w_state);
  REQUIRE(GST_IS_BUFFER(gst_buffer));
  gst_buffer_unref(gst_buffer);
  w_roundtrip_n(h.wd.get(), 3);

  // Output itself is 1920x1080 now.
  auto mode = latest_mode(drain_output_events(*h.w_objects->output_events));
  REQUIRE(mode.has_value());
  REQUIRE(mode->width == 1920);
  REQUIRE(mode->height == 1080);

  // Toplevel must still sit inside the declared 800x600 max.
  auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
  REQUIRE_FALSE(configures.empty());
  const auto &latest = configures.back();
  REQUIRE(latest.width > 0);
  REQUIRE(latest.height > 0);
  REQUIRE(latest.width <= 800);
  REQUIRE(latest.height <= 600);
}

TEST_CASE("Wayland rapid back-to-back resolution changes converge", "[WAYLAND][resolution]") {
  // Drive three resolution changes with no frames pulled between them, then pump a
  // single frame and assert the client converges on the final mode. Protects the
  // event-loop serialization: all VideoInfo commands must apply in order, and the
  // final state must be what the client observes.
  auto h = make_resolution_harness({1920, 1080, 60});

  drain_output_events(*h.w_objects->output_events);
  drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

  set_resolution(*h.w_state, {1280, 720, 60});
  set_resolution(*h.w_state, {2560, 1440, 60});
  set_resolution(*h.w_state, {800, 600, 30});

  // Only one frame pulled -- any intermediate buffer must correspond to the final
  // resolution, not a stale one from an earlier call.
  auto *gst_buffer = get_frame(*h.w_state);
  REQUIRE(GST_IS_BUFFER(gst_buffer));
  REQUIRE(gst_buffer_get_size(gst_buffer) == 800 * 600 * 4);
  gst_buffer_unref(gst_buffer);

  w_roundtrip_n(h.wd.get(), 5);

  auto mode = latest_mode(drain_output_events(*h.w_objects->output_events));
  REQUIRE(mode.has_value());
  REQUIRE(mode->width == 800);
  REQUIRE(mode->height == 600);

  auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
  REQUIRE_FALSE(configures.empty());
  REQUIRE(configures.back().width == 800);
  REQUIRE(configures.back().height == 600);
}

TEST_CASE("Wayland idempotent same-resolution VideoInfo does not break client", "[WAYLAND][resolution]") {
  // A sender may re-negotiate the same caps; calling set_resolution with identical
  // parameters must not tear down or confuse the client's output/toplevel bindings.
  auto h = make_resolution_harness({1920, 1080, 60});

  drain_output_events(*h.w_objects->output_events);
  drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);

  for (int i = 0; i < 3; ++i) {
    set_resolution(*h.w_state, {1920, 1080, 60});
    auto *gst_buffer = get_frame(*h.w_state);
    REQUIRE(GST_IS_BUFFER(gst_buffer));
    REQUIRE(gst_buffer_get_size(gst_buffer) == 1920 * 1080 * 4);
    gst_buffer_unref(gst_buffer);
    w_roundtrip_n(h.wd.get(), 2);
  }

  // Whatever events came through, they must all describe the same stable 1920x1080
  // mode -- no width/height drift from repeated same-params commands.
  auto events = drain_output_events(*h.w_objects->output_events);
  for (const auto &ev : events) {
    if (ev.type == OUTPUT_MODE) {
      INFO("stray mode: " << ev.width << "x" << ev.height);
      REQUIRE(ev.width == 1920);
      REQUIRE(ev.height == 1080);
    }
  }

  auto configures = drain_toplevel_configure_events(*h.w_objects->toplevel_configure_events);
  for (const auto &cfg : configures) {
    INFO("stray configure: " << cfg.width << "x" << cfg.height);
    REQUIRE(cfg.width == 1920);
    REQUIRE(cfg.height == 1080);
  }
}

TEST_CASE("Wayland keyboard input still delivers after resolution change", "[WAYLAND][resolution]") {
  // Catches regressions where the compositor rebuilds output state on resize and
  // accidentally drops the keyboard/seat focus or invalidates the client's
  // wl_keyboard binding.
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

  { // Put the window into focus so subsequent key events are delivered.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
    commit_frame(w_objects.get());
    wl_display_roundtrip(wd.get());

    auto mv_packet = pkts::MOUSE_MOVE_REL_PACKET{.delta_x = 10, .delta_y = 20};
    mv_packet.type = pkts::MOUSE_MOVE_REL;
    control::handle_input(session, {}, &mv_packet);
    wl_display_roundtrip(wd.get());
    // drain mouse enter/motion/frame
    while (mouse_events_q->pop(std::chrono::milliseconds(50)).has_value()) {
    }
  }

  // Change resolution mid-session.
  set_resolution(*w_state, {1280, 720, FPS});
  auto *gst_buffer = get_frame(*w_state);
  REQUIRE(GST_IS_BUFFER(gst_buffer));
  gst_buffer_unref(gst_buffer);
  w_roundtrip_n(wd.get(), 3);

  // Press + release a key after the resize; the client must still receive both.
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
}