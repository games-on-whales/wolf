#include "catch2/catch_all.hpp"
#include <csignal>
#include <streaming/wayland-display.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;
using Catch::Matchers::SizeIs;

TEST_CASE("Wayland C APIs", "[WAYLAND]") {
  auto w_state = streaming::create_wayland_display({"/dev/input/event0"});

  REQUIRE_THAT(w_state->env, SizeIs(2));
  REQUIRE_THAT(w_state->env, Contains("WAYLAND_DISPLAY=wayland-1"));
  REQUIRE_THAT(w_state->env, Contains("DISPLAY=:1"));

  REQUIRE_THAT(w_state->graphic_devices, SizeIs(2));
  REQUIRE_THAT(w_state->graphic_devices, Contains("/dev/dri/renderD128"));
  REQUIRE_THAT(w_state->graphic_devices, Contains("/dev/dri/card0"));

  SECTION("Set resolution to 1080p") {
    set_resolution(w_state, {1920, 1080, 60});

    auto gst_buffer = display_get_frame(*w_state->display);
    REQUIRE(gst_buffer_get_size(gst_buffer) == 1920 * 1080 * 4);

    gst_buffer_unref(gst_buffer);
  }

  SECTION("Changing Resolution to 720p") {
    set_resolution(w_state, {1280, 720, 30});

    auto gst_buffer = display_get_frame(*w_state->display);
    REQUIRE(gst_buffer_get_size(gst_buffer) == 1280 * 720 * 4);

    gst_buffer_unref(gst_buffer);
  }

  signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE (thrown by race condition between XWayland and Wayland on close)
}