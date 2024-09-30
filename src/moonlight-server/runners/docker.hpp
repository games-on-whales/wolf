#pragma once
#include <boost/thread/thread.hpp>
#include <chrono>
#include <control/control.hpp>
#include <core/docker.hpp>
#include <docker/formatters.hpp>
#include <events/events.hpp>
#include <fmt/core.h>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <platforms/hw.hpp>
#include <range/v3/view.hpp>
#include <state/data-structures.hpp>
#include <utility>

namespace wolf::core::docker {

using namespace std::chrono_literals;
using namespace ranges::views;
using namespace utils;
using namespace wolf::core;

class RunDocker : public events::Runner {
public:
  static RunDocker from_cfg(std::shared_ptr<events::EventBusType> ev_bus, const wolf::config::AppDocker &runner_cfg) {
    std::vector<MountPoint> mounts =
        runner_cfg.mounts                           //
        | transform([](const std::string &mount) {  //
            auto splits = utils::split(mount, ':'); //
            if (splits.size() < 2) {
              throw std::runtime_error(fmt::format("[TOML] Docker, invalid mount point definition: {}", mount));
            }
            return MountPoint{.source = to_string(splits.at(0)),
                              .destination = to_string(splits.at(1)),
                              .mode = to_string(splits.size() > 2 ? splits.at(2) : "rw")}; //
          })                                                                               //
        | ranges::to_vector;                                                               //

    std::vector<Port> ports =
        runner_cfg.ports                          //
        | transform([](const std::string &port) { //
            auto splits = utils::split(port, ':');
            if (splits.size() < 2) {
              throw std::runtime_error(fmt::format("[TOML] Docker, invalid port definition: {}", port));
            }
            PortType port_type = TCP;
            if (splits.size() > 2 && to_string(splits.at(2)) == "udp") {
              port_type = UDP;
            }
            return Port{.private_port = std::stoi(to_string(splits.at(0))),
                        .public_port = std::stoi(to_string(splits.at(1))),
                        .type = port_type};
          }) //
        | ranges::to_vector;

    std::vector<Device> devices =
        runner_cfg.devices                          //
        | transform([](const std::string &mount) {  //
            auto splits = utils::split(mount, ':'); //
            if (splits.size() < 2) {
              throw std::runtime_error(fmt::format("[TOML] Docker, invalid device definition: {}", mount));
            }
            return Device{.path_on_host = to_string(splits.at(0)),
                          .path_in_container = to_string(splits.at(1)),
                          .cgroup_permission = to_string(splits.size() > 2 ? splits.at(2) : "mrw")}; //
          })                                                                                         //
        | ranges::to_vector;                                                                         //

    auto docker_socket = utils::get_env("WOLF_DOCKER_SOCKET", "/var/run/docker.sock");
    return RunDocker(std::move(ev_bus),
                     runner_cfg.base_create_json.value_or(R"({
"HostConfig": {
  "IpcMode": "host",
}
})"),
                     Container{.id = "",
                               .name = runner_cfg.name,
                               .image = runner_cfg.image,
                               .status = docker::CREATED,
                               .ports = ports,
                               .mounts = mounts,
                               .devices = devices,
                               .env = runner_cfg.env},
                     docker_socket);
  }

  void run(std::size_t session_id,
           std::string_view app_state_folder,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override;

  rfl::TaggedUnion<"type", wolf::config::AppCMD, wolf::config::AppDocker, wolf::config::AppChildSession> serialize() override {
    return wolf::config::AppDocker{
        .name = container.name,
        .image = container.image,
        .mounts = container.mounts | transform([](const auto &el) { return fmt::format("{}", el); }) |
                  ranges::to_vector,
        .env = container.env,
        .devices = container.devices | transform([](const auto &el) { return fmt::format("{}", el); }) |
                   ranges::to_vector,
        .ports = container.ports | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector,
        .base_create_json = base_create_json};
  }

protected:
  RunDocker(std::shared_ptr<events::EventBusType> ev_bus,
            std::string base_create_json,
            docker::Container base_container,
            std::string docker_socket)
      : ev_bus(std::move(ev_bus)), container(std::move(base_container)), base_create_json(std::move(base_create_json)),
        docker_api(std::move(docker_socket)) {}

private:
  std::shared_ptr<events::EventBusType> ev_bus;
  docker::Container container;
  std::string base_create_json;
  docker::DockerAPI docker_api;
};

} // namespace wolf::core::docker
