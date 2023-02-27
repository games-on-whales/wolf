#pragma once

#include <docker/docker.hpp>
#include <fmt/format.h>
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
  object const &obj = jv.as_object();
  return docker::MountPoint{
      .source = json::value_to<std::string>(obj.at("Source")),
      .destination = json::value_to<std::string>(obj.at("Destination")),
      .mode = json::value_to<std::string>(obj.at("Mode")),
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

  std::vector<docker::Port> ports;
  auto bindings = obj.at("HostConfig").at("PortBindings");
  if (!bindings.is_null()) {
    for (auto const &port : bindings.as_object()) {
      auto settings = utils::split(std::string_view(port.key().data(), port.key().size()), '/');
      ports.push_back(
          docker::Port{.private_port = std::stoi(port.value().as_array()[0].at("HostPort").as_string().data()),
                       .public_port = std::stoi(settings[0].data()),
                       .type = settings[1] == "tcp" ? docker::TCP : docker::UDP});
    }
  }

  return docker::Container{.id = json::value_to<std::string>(obj.at("Id")),
                           .name = json::value_to<std::string>(obj.at("Name")),
                           .image = json::value_to<std::string>(obj.at("Config").at("Image")),
                           .status = status,
                           .ports = ports,
                           .mounts = json::value_to<std::vector<docker::MountPoint>>(obj.at("Mounts")),
                           .env = json::value_to<std::vector<std::string>>(obj.at("Config").at("Env"))};
}
} // namespace boost::json

namespace fmt {

template <> struct formatter<docker::Port> {
public:
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    return ctx.end();
  }
  template <typename FormatContext>
  auto format(const docker::Port &port, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_to(ctx.out(),
                     "{}:{}/{}",
                     port.private_port,
                     port.public_port,
                     port.type == docker::TCP ? "tcp" : "udp");
  }
};

template <> struct formatter<docker::MountPoint> {
public:
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    return ctx.end();
  }
  template <typename FormatContext>
  auto format(const docker::MountPoint &mount, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_to(ctx.out(), "{}:{}:{}", mount.source, mount.destination, mount.mode);
  }
};

template <> struct formatter<docker::Container> {
public:
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    return ctx.end();
  }
  template <typename FormatContext>
  auto format(const docker::Container &container, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_to(ctx.out(),
                     "{{\n id: {}\n name: {}\n image: {}\n status: {}\n ports: {}\n mounts: {}\n env: {}\n}}",
                     container.id,
                     container.name,
                     container.image,
                     container.status,
                     container.ports,
                     container.mounts,
                     container.env);
  }
};
} // namespace fmt