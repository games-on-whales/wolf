#include "catch2/catch_all.hpp"
#include <platforms/hw.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;
using Catch::Matchers::SizeIs;

TEST_CASE("libdrm find linked devices", "[NVIDIA]") {
  auto devices = linked_devices("/dev/dri/renderD128");

  REQUIRE_THAT(devices, SizeIs(6));
  REQUIRE_THAT(devices, Contains("/dev/dri/card0"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia0"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia-modeset"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia-uvm"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia-uvm-tools"));
  REQUIRE_THAT(devices, Contains("/dev/nvidiactl"));

  REQUIRE_THAT(linked_devices("/dev/dri/a_non_existing_thing"), SizeIs(0));
  REQUIRE_THAT(linked_devices("software"), SizeIs(0));
}

TEST_CASE("libpci get vendor", "[NVIDIA]") {
  REQUIRE(get_vendor("/dev/dri/renderD128") == NVIDIA);
  REQUIRE(get_vendor("/dev/dri/a_non_existing_thing") == UNKNOWN);
  REQUIRE(get_vendor("software") == UNKNOWN);
}