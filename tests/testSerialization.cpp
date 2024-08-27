#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <events/events.hpp>
#include <events/reflectors.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <rfl/msgpack.hpp>

using Catch::Matchers::Equals;
using namespace wolf::core;

TEST_CASE("Serialize to JSON", "[serialization]") {
  SECTION("example from the README") {

    struct Person {
      std::string first_name;
      std::string last_name;
      int age;
    };

    const auto homer = Person{.first_name = "Homer", .last_name = "Simpson", .age = 45};

    REQUIRE_THAT(rfl::json::write(homer), Equals("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}"));
  }

  SECTION("Wolf events") {
    events::EventTypes event = events::PlugDeviceEvent{.session_id = 123,
                                                       .udev_events = {{{"add", "usb"}}},
                                                       .udev_hw_db_entries = {{"usb", {"usb1", "usb2"}}}};

    REQUIRE_THAT(rfl::json::write(event),
                 Equals("{\"session_id\":123,"
                        "\"udev_events\":[{\"add\":\"usb\"}],"
                        "\"udev_hw_db_entries\":[[\"usb\",[\"usb1\",\"usb2\"]]]}"));

    event = events::PairSignal{.client_ip = "192.168.1.1", .host_ip = "0.0.0.0"};

    REQUIRE_THAT(rfl::json::write(event), Equals("{\"client_ip\":\"192.168.1.1\",\"host_ip\":\"0.0.0.0\"}"));
  }

  // TODO: test the inverse operation, rfl::json::read
}

TEST_CASE("Serialize to msgpack", "[serialization]") {
  SECTION("example from the README") {

    struct Person {
      std::string first_name;
      std::string last_name;
      int age;
    };

    const auto homer = Person{.first_name = "Homer", .last_name = "Simpson", .age = 45};

    auto result = rfl::msgpack::read<Person>(rfl::msgpack::write(homer));

    REQUIRE_THAT(result.value().first_name, Equals("Homer"));
    REQUIRE_THAT(result.value().last_name, Equals("Simpson"));
    REQUIRE(result.value().age == 45);
  }
}