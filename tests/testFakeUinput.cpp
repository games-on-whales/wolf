#include <api/fake_uinput.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace wolf::api::fake_uinput;
namespace fs = std::filesystem;

// Build a throwaway sysfs-style input dir with capabilities/{key,rel} files.
static fs::path make_input_dir(const std::string &key_caps, const std::string &rel_caps = "") {
  static int n = 0;
  auto dir = fs::temp_directory_path() / ("fu-test-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
  fs::create_directories(dir / "capabilities");
  std::ofstream(dir / "capabilities" / "key") << key_caps;
  if (!rel_caps.empty())
    std::ofstream(dir / "capabilities" / "rel") << rel_caps;
  return dir;
}

TEST_CASE("fake-uinput read_caps / has_bit parse hex bitmasks", "[fake-uinput]") {
  // Space-separated 64-bit hex words, high group first. "1<<48" in the 5th word == bit 0x130.
  auto dir = make_input_dir("1000000000000 0 0 0 0");
  auto caps = read_caps(dir / "capabilities" / "key");
  REQUIRE(has_bit(caps, 0x130)); // BTN_GAMEPAD
  REQUIRE_FALSE(has_bit(caps, 0x131));
  REQUIRE_FALSE(has_bit(caps, 9999)); // out of range must not read OOB
  fs::remove_all(dir);
}

TEST_CASE("fake-uinput classify from sysfs capabilities", "[fake-uinput]") {
  SECTION("js* nodes are always joysticks (joydev, no evdev caps)") {
    auto dir = make_input_dir("0");
    const char *c = classify(dir, "js0");
    REQUIRE(c != nullptr);
    REQUIRE(std::string(c) == "ID_INPUT_JOYSTICK");
    fs::remove_all(dir);
  }
  SECTION("BTN_GAMEPAD -> joystick") {
    auto dir = make_input_dir("1000000000000 0 0 0 0");
    const char *c = classify(dir, "event5");
    REQUIRE(c != nullptr);
    REQUIRE(std::string(c) == "ID_INPUT_JOYSTICK");
    fs::remove_all(dir);
  }
  SECTION("KEY_A + KEY_Z -> keyboard") {
    auto dir = make_input_dir("100040000000"); // bits 30 (KEY_A) and 44 (KEY_Z)
    const char *c = classify(dir, "event6");
    REQUIRE(c != nullptr);
    REQUIRE(std::string(c) == "ID_INPUT_KEYBOARD");
    fs::remove_all(dir);
  }
  SECTION("no recognized capabilities -> unclassified") {
    auto dir = make_input_dir("0");
    REQUIRE(classify(dir, "event7") == nullptr);
    fs::remove_all(dir);
  }
}
