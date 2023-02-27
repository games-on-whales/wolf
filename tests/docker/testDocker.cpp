#include "catch2/catch_all.hpp"
using Catch::Matchers::Equals;

#include <docker/docker.hpp>

TEST_CASE("Docker API", "DOCKER") {
  docker::init();

  docker::Container container = {
      .id = "",
      .name = "WolfTestHelloWorld",
      .image = "hello-world",
      .status = docker::CREATED,
      .ports = {docker::Port{.private_port = 1234, .public_port = 1235, .type = docker::TCP}},
      .mounts = {docker::MountPoint{.source = "/tmp/", .destination = "/tmp/", .mode = "ro"}},
      .env = {"ASD=true"}};
  auto result = docker::create(container);
  // auto containers = docker::get_containers(true);
  REQUIRE(result.has_value());
  REQUIRE(docker::start_by_id(result.value().id));
  REQUIRE(docker::stop_by_id(result.value().id));
  REQUIRE(docker::remove_by_id(result.value().id));
}