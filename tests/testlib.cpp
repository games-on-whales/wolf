#define CATCH_CONFIG_MAIN
#include <boost/property_tree/xml_parser.hpp>
#include <catch2/catch.hpp>
#include <moonlight/protocol.hpp>
#include <simple-pair.cpp>

using namespace moonlight;

TEST_CASE("LocalState load JSON", "[LocalState]") {
  auto state = new Config("config.json");
  REQUIRE(state->hostname() == "test_wolf");
  REQUIRE(state->get_uuid() == "uid-12345");
  REQUIRE(state->external_ip() == "192.168.99.1");
  REQUIRE(state->local_ip() == "192.168.1.1");
  REQUIRE(state->mac_address() == "AA:BB:CC:DD");

  SECTION("Port mapping") {
    REQUIRE(state->map_port(Config::HTTP_PORT) == 3000);
    REQUIRE(state->map_port(Config::HTTPS_PORT) == 2995);
  }
}

TEST_CASE("Mocked serverinfo", "[MoonlightProtocol]") {
  auto state = new Config("config.json");
  std::vector<DisplayMode> displayModes = {{1920, 1080, 60}, {1024, 768, 30}};
  auto pair_handler = new SimplePair();

  SECTION("server_info conforms with the expected server_info_response.xml") {
    auto result = serverinfo(*state, *pair_handler, false, 0, displayModes, "001122");
    pt::ptree expectedResult;
    pt::read_xml("server_info_response.xml", expectedResult, boost::property_tree::xml_parser::trim_whitespace);

    REQUIRE(result == expectedResult);
    REQUIRE(result.get<bool>("root.PairStatus") == false);
  }

  SECTION("does pairing change the returned serverinfo?") {
    pair_handler->pair("001122", "", "", "");
    auto result = serverinfo(*state, *pair_handler, false, 0, displayModes, "001122");

    REQUIRE(result.get<bool>("root.PairStatus") == true);
  }
}