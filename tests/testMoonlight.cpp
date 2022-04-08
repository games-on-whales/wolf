#include <boost/property_tree/xml_parser.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <moonlight/crypto.hpp>
#include <moonlight/protocol.hpp>

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

TEST_CASE("LocalState pairing information", "[LocalState]") {
  auto state = new Config("config.json");
  auto clientID = "0123456789ABCDEF";
  auto a_client_cert = "A DUMP OF A VALID CERTIFICATE";

  SECTION("Checking pairing mechanism") {
    REQUIRE(state->get_paired_clients().empty());
    REQUIRE(state->isPaired(clientID) == false);

    state->pair("Another client", a_client_cert);
    REQUIRE(state->isPaired(clientID) == false);
    state->pair(clientID, a_client_cert);
    REQUIRE(state->isPaired(clientID) == true);
    REQUIRE(state->isPaired("Another client") == true);

    REQUIRE_THAT(state->get_paired_clients(), Catch::Matchers::SizeIs(2)); // TODO: check content
  }

  SECTION("Checking client cert info") {
    REQUIRE(state->get_client_cert(clientID) == std::nullopt);

    state->pair(clientID, a_client_cert);
    state->pair("Another client", a_client_cert);

    REQUIRE(state->get_client_cert(clientID) == a_client_cert);
    REQUIRE(state->get_client_cert(clientID) == a_client_cert);
    REQUIRE(state->get_client_cert("Another client") == a_client_cert);
    REQUIRE(state->get_client_cert("A non existent client") == std::nullopt);
  }
}

TEST_CASE("Mocked serverinfo", "[MoonlightProtocol]") {
  auto state = new Config("config.json");
  std::vector<DisplayMode> displayModes = {{1920, 1080, 60}, {1024, 768, 30}};

  SECTION("server_info conforms with the expected server_info_response.xml") {
    auto result = serverinfo(*state, false, 0, displayModes, "001122");
    pt::ptree expectedResult;
    pt::read_xml("server_info_response.xml", expectedResult, boost::property_tree::xml_parser::trim_whitespace);

    REQUIRE(result == expectedResult);
    REQUIRE(result.get<bool>("root.PairStatus") == false);
  }

  SECTION("does pairing change the returned serverinfo?") {
    state->pair("001122", "");
    auto result = serverinfo(*state, false, 0, displayModes, "001122");

    REQUIRE(result.get<bool>("root.PairStatus") == true);
  }
}
