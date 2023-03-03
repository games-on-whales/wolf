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
      .devices = {docker::Device{.path_on_host = "/dev/input/mice",
                                 .path_in_container = "/dev/input/mice",
                                 .cgroup_permission = "mrw"}},
      .env = {"ASD=true"}};

  auto first_container = docker::create(container);
  REQUIRE(first_container.has_value());
  REQUIRE(docker::start_by_id(first_container.value().id));
  REQUIRE(docker::stop_by_id(first_container.value().id));

  // This should remove the first container and create a new one with the same name
  auto second_container = docker::create(first_container.value());
  REQUIRE(second_container.has_value());
  REQUIRE(first_container->id != second_container->id);
  REQUIRE(first_container->name == second_container->name);
  REQUIRE(!docker::remove_by_id(first_container->id)); // This container doesn't exist anymore
  REQUIRE(docker::remove_by_id(second_container->id));
}