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
  static RunDocker from_toml(std::shared_ptr<dp::event_bus> ev_bus, const toml::value &runner_obj) {
    std::vector<std::string> rec_mounts = toml::find_or<std::vector<std::string>>(runner_obj, "mounts", {});
    std::vector<MountPoint> mounts =
        rec_mounts                                  //
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

    std::vector<std::string> rec_ports = toml::find_or<std::vector<std::string>>(runner_obj, "ports", {});
    std::vector<Port> ports =
        rec_ports                                 //
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

    std::vector<std::string> rec_devices = toml::find_or<std::vector<std::string>>(runner_obj, "devices", {});
    std::vector<Device> devices =
        rec_devices                                 //
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

    auto default_socket = utils::get_env("WOLF_DOCKER_SOCKET", "/var/run/docker.sock");
    auto docker_socket = toml::find_or<std::string>(runner_obj, "docker_socket", default_socket);

    return RunDocker(std::move(ev_bus),
                     toml::find_or<std::string>(runner_obj, "base_create_json", R"({
                        "HostConfig": {
                          "IpcMode": "host",
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
           std::string_view app_state_folder,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override;

  toml::value serialise() override {
    return toml::table{
        {"type", "docker"},
        {"name", container.name},
        {"image", container.image},
        {"ports",
         container.ports | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector},
        {"mounts",
         container.mounts | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector},
        {"devices",
         container.devices | transform([](const auto &el) { return fmt::format("{}", el); }) | ranges::to_vector},
        {"env", container.env},
        {"base_create_json", base_create_json}};
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

void create_udev_hw_files(std::filesystem::path base_hw_db_path,
                          std::vector<std::pair<std::string, std::vector<std::string>>> udev_hw_db_entries) {
  for (const auto &[filename, content] : udev_hw_db_entries) {
    auto host_file_path = (base_hw_db_path / filename).string();
    logs::log(logs::debug, "[DOCKER] Writing hwdb file: {}", host_file_path);
    std::ofstream host_file(host_file_path);
    host_file << utils::join(content, "\n");
    host_file.close();
  }
}

void RunDocker::run(std::size_t session_id,
                    std::string_view app_state_folder,
                    std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
                    const immer::array<std::string> &virtual_inputs,
                    const immer::array<std::pair<std::string, std::string>> &paths,
                    const immer::map<std::string, std::string> &env_variables,
                    std::string_view render_node) {

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

  // Fake udev
  auto udev_base_path = std::filesystem::path(app_state_folder) / "udev";
  auto hw_db_path = udev_base_path / "data";
  auto fake_udev_cli_path = std::string(utils::get_env("WOLF_DOCKER_FAKE_UDEV_PATH", ""));
  bool use_fake_udev = !fake_udev_cli_path.empty() || std::filesystem::exists(fake_udev_cli_path);
  if (use_fake_udev) {
    logs::log(logs::debug, "[DOCKER] Using fake-udev, creating {}", hw_db_path.string());
    std::filesystem::create_directories(hw_db_path);

    // Check if /run/udev/control exists
    auto udev_ctrl_path = udev_base_path / "control";
    if (!std::filesystem::exists(udev_ctrl_path)) {
      if (auto control_file = std::ofstream(udev_ctrl_path)) {
        control_file.close();
        std::filesystem::permissions(udev_ctrl_path, std::filesystem::perms::all); // set 777
      }
    }
    mounts.push_back(MountPoint{.source = udev_base_path.string(), .destination = "/run/udev/", .mode = "rw"});
    mounts.push_back(MountPoint{.source = fake_udev_cli_path, .destination = "/usr/bin/fake-udev", .mode = "ro"});
  } else {
    logs::log(logs::warning,
              "[DOCKER] Unable to use fake-udev, check the env variable WOLF_DOCKER_FAKE_UDEV_PATH and the file at {}",
              fake_udev_cli_path);
  }

  // Add equivalent of --gpu=all if on NVIDIA without the custom driver volume
  auto final_json_opts = this->base_create_json;
  if (get_vendor(render_node) == NVIDIA && !utils::get_env("NVIDIA_DRIVER_VOLUME_NAME")) {
    logs::log(logs::info, "NVIDIA_DRIVER_VOLUME_NAME not set, assuming nvidia driver toolkit is installed..");
    {
      auto parsed_json = utils::parse_json(final_json_opts).as_object();
      auto default_gpu_config = boost::json::array{                    // [
                                                   boost::json::object{// {
                                                                       {"Driver", "nvidia"},
                                                                       {"DeviceIDs", {"all"}},
                                                                       {"Capabilities", boost::json::array{{"gpu"}}}}};
      if (auto host_config_ptr = parsed_json.if_contains("HostConfig")) {
        auto host_config = host_config_ptr->as_object();
        if (host_config.find("DeviceRequests") == host_config.end()) {
          host_config["DeviceRequests"] = default_gpu_config;
          parsed_json["HostConfig"] = host_config;
          final_json_opts = boost::json::serialize(parsed_json);
        } else {
          logs::log(logs::debug, "DeviceRequests manually set in base_create_json, skipping..");
        }
      } else {
        logs::log(logs::warning, "HostConfig not found in base_create_json.");
        parsed_json["HostConfig"] = boost::json::object{{"DeviceRequests", default_gpu_config}};
        final_json_opts = boost::json::serialize(parsed_json);
      }
    }

    // Setup -e NVIDIA_VISIBLE_DEVICES=all  -e NVIDIA_DRIVER_CAPABILITIES=all if not present
    {
      auto nvd_env = std::find_if(full_env.begin(), full_env.end(), [](const std::string &env) {
        return env.find("NVIDIA_VISIBLE_DEVICES") != std::string::npos;
      });
      if (nvd_env == full_env.end()) {
        full_env.push_back("NVIDIA_VISIBLE_DEVICES=all");
      }

      auto nvd_caps_env = std::find_if(full_env.begin(), full_env.end(), [](const std::string &env) {
        return env.find("NVIDIA_DRIVER_CAPABILITIES") != std::string::npos;
      });
      if (nvd_caps_env == full_env.end()) {
        full_env.push_back("NVIDIA_DRIVER_CAPABILITIES=all");
      }
    }
  }

  Container new_container = {.id = "",
                             .name = fmt::format("{}_{}", this->container.name, session_id),
                             .image = this->container.image,
                             .status = CREATED,
                             .ports = this->container.ports,
                             .mounts = mounts,
                             .devices = devices,
                             .env = full_env};

  if (auto docker_container = docker_api.create(new_container, final_json_opts)) {
    auto container_id = docker_container->id;
    docker_api.start_by_id(container_id);

    logs::log(logs::info, "[DOCKER] Starting container: {}", docker_container->name);
    logs::log(logs::debug, "[DOCKER] Starting container: {}", *docker_container);

    auto terminate_handler = this->ev_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [session_id, container_id, this](const immer::box<events::StopStreamEvent> &terminate_ev) {
          if (terminate_ev->session_id == session_id) {
            docker_api.stop_by_id(container_id);
          }
        });

    auto unplug_device_handler = this->ev_bus->register_handler<immer::box<events::UnplugDeviceEvent>>(
        [session_id, container_id, hw_db_path, this](const immer::box<events::UnplugDeviceEvent> &ev) {
          if (ev->session_id == session_id) {
            for (const auto &[filename, content] : ev->udev_hw_db_entries) {
              std::filesystem::remove(hw_db_path / filename);
            }

            for (auto udev_ev : ev->udev_events) {
              udev_ev["ACTION"] = "remove";
              std::string udev_msg = base64_encode(map_to_string(udev_ev));
              std::string cmd;
              if (udev_ev.count("DEVNAME") == 0) {
                cmd = fmt::format("fake-udev -m {}", udev_msg);
              } else {
                cmd = fmt::format("fake-udev -m {} && rm {}", udev_msg, udev_ev["DEVNAME"]);
              }
              logs::log(logs::debug, "[DOCKER] Executing command: {}", cmd);
              docker_api.exec(container_id, {"/bin/bash", "-c", cmd}, "root");
            }
          }
        });

    do {
      // Plug all devices that are waiting in the queue
      while (auto device_ev = plugged_devices_queue->pop(50ms)) {
        if (device_ev->get().session_id == session_id) {
          if (use_fake_udev) {
            create_udev_hw_files(hw_db_path, device_ev->get().udev_hw_db_entries);
          }

          for (auto udev_ev : device_ev->get().udev_events) {
            std::string cmd;
            std::string udev_msg = base64_encode(map_to_string(udev_ev));
            if (udev_ev.count("DEVNAME") == 0) {
              cmd = fmt::format("fake-udev -m {}", udev_msg);
            } else {
              cmd = fmt::format("mkdir -p /dev/input && mknod {} c {} {} && chmod 777 {} && fake-udev -m {}",
                                udev_ev["DEVNAME"],
                                udev_ev["MAJOR"],
                                udev_ev["MINOR"],
                                udev_ev["DEVNAME"],
                                udev_msg);
            }
            logs::log(logs::debug, "[DOCKER] Executing command: {}", cmd);
            docker_api.exec(container_id, {"/bin/bash", "-c", cmd}, "root");
          }
        }
      }

      std::this_thread::sleep_for(500ms);

    } while (docker_api.get_by_id(container_id)->status == RUNNING);

    logs::log(logs::debug, "[DOCKER] Container logs: \n{}", docker_api.get_logs(container_id));
    logs::log(logs::debug, "[DOCKER] Stopping container: {}", docker_container->name);
    if (const auto env = utils::get_env("WOLF_STOP_CONTAINER_ON_EXIT")) {
      if (std::string(env) == "TRUE") {
        docker_api.stop_by_id(container_id);
        docker_api.remove_by_id(container_id);
      }
    }
    logs::log(logs::info, "Stopped container: {}", docker_container->name);
    std::filesystem::remove_all(udev_base_path);
    terminate_handler.unregister();
  }
}

} // namespace wolf::core::docker
