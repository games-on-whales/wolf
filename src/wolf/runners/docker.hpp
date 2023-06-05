#pragma once
#include <boost/thread/thread.hpp>
#include <chrono>
#include <docker/docker.hpp>
#include <docker/formatters.hpp>
#include <fmt/core.h>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <range/v3/view.hpp>
#include <state/data-structures.hpp>
#include <utility>

namespace docker {

using namespace std::chrono_literals;
using namespace ranges::views;
using namespace utils;

class RunDocker : public state::Runner {
public:
  static RunDocker from_toml(std::shared_ptr<dp::event_bus> ev_bus, const toml::value &runner_obj) {
    std::vector<std::string> rec_mounts = toml::find_or<std::vector<std::string>>(runner_obj, "mounts", {});
    std::vector<MountPoint> mounts = rec_mounts                                  //
                                     | transform([](const std::string &mount) {  //
                                         auto splits = utils::split(mount, ':'); //
                                         return MountPoint{.source = to_string(splits[0]),
                                                           .destination = to_string(splits[1]),
                                                           .mode = to_string(splits[2])}; //
                                       })                                                 //
                                     | ranges::to_vector;                                 //

    std::vector<std::string> rec_ports = toml::find_or<std::vector<std::string>>(runner_obj, "ports", {});
    std::vector<Port> ports = rec_ports                                 //
                              | transform([](const std::string &port) { //
                                  auto splits = utils::split(port, ':');
                                  return Port{.private_port = std::stoi(to_string(splits[0])),
                                              .public_port = std::stoi(to_string(splits[1])),
                                              .type = splits[2] == "tcp" ? TCP : UDP};
                                }) //
                              | ranges::to_vector;

    std::vector<std::string> rec_devices = toml::find_or<std::vector<std::string>>(runner_obj, "devices", {});
    std::vector<Device> devices = rec_devices                                 //
                                  | transform([](const std::string &mount) {  //
                                      auto splits = utils::split(mount, ':'); //
                                      return Device{.path_on_host = to_string(splits[0]),
                                                    .path_in_container = to_string(splits[1]),
                                                    .cgroup_permission = to_string(splits[2])}; //
                                    })                                                          //
                                  | ranges::to_vector;                                          //

    auto default_socket = std::getenv("WOLF_DOCKER_SOCKET") ? std::getenv("WOLF_DOCKER_SOCKET")
                                                            : "/var/run/docker.sock";
    auto docker_socket = toml::find_or<std::string>(runner_obj, "docker_socket", default_socket);

    return RunDocker(std::move(ev_bus),
                     toml::find_or<std::string>(runner_obj, "base_create_json", R"({
                        "HostConfig": {
                          "IpcMode": "host",
                          "DeviceRequests": [{"Driver":"","Count":-1,"Capabilities":[["gpu"]]}]
                        }
                      })"),
                     Container{.id = "",
                               .name = toml::find<std::string>(runner_obj, "name"),
                               .image = toml::find<std::string>(runner_obj, "image"),
                               .status = docker::CREATED,
                               .ports = ports,
                               .mounts = mounts,
                               .devices = devices,
                               .env = toml::find_or<std::vector<std::string>>(runner_obj, "env", {})},
                     docker_socket);
  }

  void run(std::size_t session_id,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables) override;

  toml::value serialise() override {
    return {{"type", "docker"},
            {"name", container.name},
            {"image", container.image},
            {"ports",
             container.ports | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector},
            {"mounts",
             container.mounts | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector},
            {"devices",
             container.devices | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector},
            {"env", container.env}};
  }

protected:
  RunDocker(std::shared_ptr<dp::event_bus> ev_bus,
            std::string base_create_json,
            docker::Container base_container,
            std::string docker_socket)
      : ev_bus(std::move(ev_bus)), container(std::move(base_container)), base_create_json(std::move(base_create_json)),
        docker_api(std::move(docker_socket)) {}

  std::shared_ptr<dp::event_bus> ev_bus;
  docker::Container container;
  std::string base_create_json;
  docker::DockerAPI docker_api;
};

void RunDocker::run(std::size_t session_id,
                    const immer::array<std::string> &virtual_inputs,
                    const immer::array<std::pair<std::string, std::string>> &paths,
                    const immer::map<std::string, std::string> &env_variables) {

  std::vector<std::string> full_env;
  full_env.insert(full_env.end(), this->container.env.begin(), this->container.env.end());
  for (const auto &env_var : env_variables) {
    full_env.push_back(fmt::format("{}={}", env_var.first, env_var.second));
  }

  std::vector<Device> devices;
  devices.insert(devices.end(), this->container.devices.begin(), this->container.devices.end());
  for (const auto &v_input : virtual_inputs) {
    devices.push_back(Device{.path_on_host = to_string(v_input),
                             .path_in_container = to_string(v_input),
                             .cgroup_permission = "mrw"});
  }

  std::vector<MountPoint> mounts;
  mounts.insert(mounts.end(), this->container.mounts.begin(), this->container.mounts.end());
  for (const auto &path : paths) {
    mounts.insert(mounts.end(), MountPoint{.source = path.first, .destination = path.second, .mode = "rw"});
  }

  Container new_container = {.id = "",
                             .name = fmt::format("{}_{}", this->container.name, session_id),
                             .image = this->container.image,
                             .status = CREATED,
                             .ports = this->container.ports,
                             .mounts = mounts,
                             .devices = devices,
                             .env = full_env};

  if (auto docker_container = docker_api.create(new_container, this->base_create_json)) {
    auto container_id = docker_container->id;
    docker_api.start_by_id(container_id);

    logs::log(logs::info, "Starting container: {}", docker_container->name);
    logs::log(logs::debug, "Starting container: {}", *docker_container);

    auto terminate_handler = this->ev_bus->register_handler<immer::box<moonlight::StopStreamEvent>>(
        [session_id, container_id, this](const immer::box<moonlight::StopStreamEvent> &terminate_ev) {
          if (terminate_ev->session_id == session_id) {
            docker_api.stop_by_id(container_id);
          }
        });

    do {
      boost::this_thread::sleep_for(boost::chrono::milliseconds(300));
    } while (docker_api.get_by_id(container_id)->status == RUNNING);

    logs::log(logs::debug, "Container logs: \n{}", docker_api.get_logs(container_id));
    logs::log(logs::debug, "Stopping container: {}", docker_container->name);
    if (const auto env = std::getenv("WOLF_STOP_CONTAINER_ON_EXIT")) {
      if (std::string(env) == "TRUE") {
        docker_api.stop_by_id(container_id);
        docker_api.remove_by_id(container_id);
      }
    }
    logs::log(logs::info, "Stopped container: {}", docker_container->name);
    terminate_handler.unregister();
  }
}

} // namespace docker
