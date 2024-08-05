#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_contains.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <filesystem>
#include <helpers/logger.hpp>
#include <platforms/hw.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using Catch::Matchers::SizeIs;

std::string get_nvidia_render_device() {
  // iterate over /dev/dri/renderD12* devices
  for (const auto &entry : std::filesystem::directory_iterator("/dev/dri")) {
    if (entry.path().filename().string().find("renderD12") != std::string::npos) {
      auto vendor = get_vendor(entry.path().string());
      logs::log(logs::info, "Found {} with vendor {}", entry.path().string(), (int)vendor);
      if (vendor == NVIDIA) {
        return entry.path().string();
      }
    }
  }
  return "";
}

TEST_CASE("libdrm find linked devices", "[NVIDIA]") {
  std::string nvidia_node = get_nvidia_render_device();
  REQUIRE(!nvidia_node.empty()); // If there's no nvidia node, we can't continue

  auto devices = linked_devices(nvidia_node);

  REQUIRE_THAT(devices, SizeIs(6));
  REQUIRE_THAT(devices, Contains(ContainsSubstring("/dev/dri/card")));
  REQUIRE_THAT(devices, Contains("/dev/nvidia0"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia-modeset"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia-uvm"));
  REQUIRE_THAT(devices, Contains("/dev/nvidia-uvm-tools"));
  REQUIRE_THAT(devices, Contains("/dev/nvidiactl"));

  REQUIRE_THAT(linked_devices("/dev/dri/a_non_existing_thing"), SizeIs(0));
  REQUIRE_THAT(linked_devices("software"), SizeIs(0));
}

TEST_CASE("libpci get vendor", "[NVIDIA]") {
  std::string nvidia_node = get_nvidia_render_device();
  REQUIRE(!nvidia_node.empty()); // If there's no nvidia node, we can't continue

  REQUIRE(get_vendor(nvidia_node) == NVIDIA);
  REQUIRE(get_vendor("/dev/dri/a_non_existing_thing") == UNKNOWN);
  REQUIRE(get_vendor("software") == UNKNOWN);
}