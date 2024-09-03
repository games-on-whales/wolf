#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_contains.hpp>

using Catch::Matchers::Contains;
using Catch::Matchers::Equals;

#include <core/docker.hpp>
#include <runners/docker.hpp>

TEST_CASE("Docker API", "DOCKER") {
  docker::init();
  docker::DockerAPI docker_api;

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

  auto first_container = docker_api.create(container);
  REQUIRE(first_container.has_value());
  REQUIRE(docker_api.start_by_id(first_container.value().id));
  REQUIRE(docker_api.stop_by_id(first_container.value().id));

  // This should remove the first container and create a new one with the same name
  auto second_container = docker_api.create(first_container.value(), R"({
    "Env": ["AN_ENV_VAR=true"],
    "HostConfig": {
      "IpcMode": "host"
    }
  })");
  REQUIRE(second_container.has_value());
  REQUIRE(first_container->id != second_container->id);
  REQUIRE(first_container->name == second_container->name);

  REQUIRE_THAT(second_container->env, Contains("AN_ENV_VAR=true"));
  REQUIRE_THAT(second_container->env, Contains("ASD=true"));

  REQUIRE(second_container->ports.size() == first_container->ports.size());
  REQUIRE(second_container->devices.size() == first_container->devices.size());
  REQUIRE(second_container->mounts.size() == first_container->mounts.size());

  REQUIRE(!docker_api.remove_by_id(first_container->id)); // This container doesn't exist anymore
  REQUIRE(docker_api.remove_by_id(second_container->id));
}

TEST_CASE("Docker TOML", "DOCKER") {
  docker::init();
  docker::DockerAPI docker_api;

  auto event_bus = std::make_shared<events::EventBusType>();
  std::string toml_cfg = R"(

    type = "docker"
    name = "WolfTestHelloWorld"
    image = "hello-world"
    mounts = [
      "/tmp/sockets:/tmp/.X11-unix/",
      "/tmp/sockets:/run/user/1000/pulse/:ro"
    ]
    devices = [
      "/dev/input/mice:/dev/input/mice:ro",
      "/a/b/c:/d/e/f",
      "/tmp:/tmp:rw",
    ]
    ports = [
      "1234:1235",
      "1234:1235:udp"
    ]
    env = [
      "LOG_LEVEL=info"
    ]
    base_create_json = "{'HostConfig': {}}"

    )";
  std::istringstream is(toml_cfg, std::ios_base::binary | std::ios_base::in);
  auto container = docker::RunDocker::from_toml(event_bus, toml::parse(is, "std::string")).serialise();

  REQUIRE_THAT(container.at("type").as_string(), Equals("docker"));
  REQUIRE_THAT(container.at("name").as_string(), Equals("WolfTestHelloWorld"));
  REQUIRE_THAT(container.at("image").as_string(), Equals("hello-world"));

  REQUIRE_THAT(toml::get<std::vector<std::string>>(container.at("ports")),
               Equals(std::vector<std::string>{"1234:1235/tcp", "1234:1235/udp"}));
  REQUIRE_THAT(toml::get<std::vector<std::string>>(container.at("devices")),
               Equals(std::vector<std::string>{
                   "/dev/input/mice:/dev/input/mice:ro",
                   "/a/b/c:/d/e/f:mrw",
                   "/tmp:/tmp:rw",
               }));
  REQUIRE_THAT(toml::get<std::vector<std::string>>(container.at("env")),
               Equals(std::vector<std::string>{"LOG_LEVEL=info"}));
  REQUIRE_THAT(container.at("base_create_json").as_string(), Equals("{'HostConfig': {}}"));
}