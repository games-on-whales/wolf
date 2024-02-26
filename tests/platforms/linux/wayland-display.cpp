#include "catch2/catch_all.hpp"
#include <core/virtual-display.hpp>
#include <csignal>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;
using Catch::Matchers::SizeIs;
using namespace wolf::core::virtual_display;

TEST_CASE("Wayland C APIs", "[WAYLAND]") {
  auto w_state = create_wayland_display({"/dev/input/event0"});

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