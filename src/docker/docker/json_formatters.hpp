#pragma once
#include <boost/json/src.hpp>
#include <docker/docker.hpp>
#include <fmt/core.h>
#include <helpers/utils.hpp>

namespace boost::json {

void tag_invoke(value_from_tag, value &jv, const std::vector<docker::Port> &ports) {
  object obj;
  for (const auto &port : ports) {
    auto key = fmt::format("{}/{}", port.public_port, port.type == docker::TCP ? "tcp" : "udp");
    /**
     * Format here is:

     "Ports": {
        "80/tcp": [
          {
            "HostPort": "1234"
          }
        ],
        "80/udp": [
          {
            "HostPort": "1235"
          }
        ]
        }
     */
    obj[key] = {{{"HostPort", std::to_string(port.private_port)}}};
  }
  jv = obj;
}

void tag_invoke(value_from_tag, value &jv, const docker::MountPoint &mount) {
  jv = fmt::format("{}:{}:{}", mount.source, mount.destination, mount.mode);
}

docker::MountPoint tag_invoke(value_to_tag<docker::MountPoint>, value const &jv) {
  // ex: /home/ale/repos/gow/local_state:/home/retro:rw
  auto bind = utils::split(std::string_view{jv.as_string().data(), jv.as_string().size()}, ':');
  return docker::MountPoint{
      .source = utils::to_string(bind[0]),
      .destination = utils::to_string(bind[1]),
      .mode = utils::to_string(bind.size() == 3 ? bind[2] : "rw"),
  };
}

void tag_invoke(value_from_tag, value &jv, const docker::Device &dev) {
  // example: { "PathOnHost": "/dev/deviceName", "PathInContainer": "/dev/deviceName", "CgroupPermissions": "mrw"}
  jv = {{"PathOnHost", dev.path_on_host},
        {"PathInContainer", dev.path_in_container},
        {"CgroupPermissions", dev.cgroup_permission}};
}

docker::Device tag_invoke(value_to_tag<docker::Device>, value const &jv) {
  object const &obj = jv.as_object();
  return docker::Device{
      .path_on_host = json::value_to<std::string>(obj.at("PathOnHost")),
      .path_in_container = json::value_to<std::string>(obj.at("PathInContainer")),
      .cgroup_permission = json::value_to<std::string>(obj.at("CgroupPermissions")),
  };
}

docker::Container tag_invoke(value_to_tag<docker::Container>, value const &jv) {
  object const &obj = jv.as_object();

  docker::ContainerStatus status;
  auto status_str = utils::to_lower(json::value_to<std::string>(obj.at("State").at("Status")));
  switch (utils::hash(status_str)) {
  case (utils::hash("created")):
    status = docker::CREATED;
    break;
  case (utils::hash("running")):
    status = docker::RUNNING;
    break;
  case (utils::hash("paused")):
    status = docker::PAUSED;
    break;
  case (utils::hash("restarting")):
    status = docker::RESTARTING;
    break;
  case (utils::hash("removing")):
    status = docker::REMOVING;
    break;
  case (utils::hash("exited")):
    status = docker::EXITED;
    break;
  case (utils::hash("dead")):
    status = docker::DEAD;
    break;
  }

  const auto &host_config = obj.at("HostConfig");

  std::vector<docker::Port> ports;
  if (!host_config.at("PortBindings").is_null()) { // This can be `null` in the APIs for some reason
    for (auto const &port : host_config.at("PortBindings").as_object()) {
      auto settings = utils::split(std::string_view(port.key().data(), port.key().size()), '/');
      ports.push_back(
          docker::Port{.private_port = std::stoi(port.value().as_array()[0].at("HostPort").as_string().data()),
                       .public_port = std::stoi(settings[0].data()),
                       .type = settings[1] == "tcp" ? docker::TCP : docker::UDP});
    }
  }

  std::vector<docker::MountPoint> mounts;
  if (!host_config.at("Binds").is_null()) { // This can be `null` in the APIs for some reason
    mounts = json::value_to<std::vector<docker::MountPoint>>(host_config.at("Binds"));
  }

  std::vector<docker::Device> devices;
  if (!host_config.at("Devices").is_null()) { // This can be `null` in the APIs for some reason
    devices = json::value_to<std::vector<docker::Device>>(host_config.at("Devices"));
  }

  return docker::Container{.id = json::value_to<std::string>(obj.at("Id")),
                           .name = json::value_to<std::string>(obj.at("Name")),
                           .image = json::value_to<std::string>(obj.at("Config").at("Image")),
                           .status = status,
                           .ports = ports,
                           .mounts = mounts,
                           .devices = devices,
                           .env = json::value_to<std::vector<std::string>>(obj.at("Config").at("Env"))};
}
} // namespace boost::json