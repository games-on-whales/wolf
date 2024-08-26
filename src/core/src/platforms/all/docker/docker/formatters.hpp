#pragma once

#include <core/docker.hpp>
#include <fmt/format.h>
#include <helpers/utils.hpp>

using namespace wolf::core;

namespace fmt {

template <> struct [[maybe_unused]] formatter<docker::Port> {
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

template <> struct [[maybe_unused]] formatter<docker::MountPoint> {
public:
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    return ctx.end();
  }
  template <typename FormatContext>
  auto format(const docker::MountPoint &mount, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_to(ctx.out(), fmt::runtime("{}:{}:{}"), mount.source, mount.destination, mount.mode);
  }
};

template <> struct [[maybe_unused]] formatter<docker::Device> {
public:
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    return ctx.end();
  }
  template <typename FormatContext>
  auto format(const docker::Device &dev, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_to(ctx.out(),
                     fmt::runtime("{}:{}:{}"),
                     dev.path_on_host,
                     dev.path_in_container,
                     dev.cgroup_permission);
  }
};

template <> struct [[maybe_unused]] formatter<docker::Container> {
public:
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    return ctx.end();
  }
  template <typename FormatContext>
  auto format(const docker::Container &container, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_to(
        ctx.out(),
        fmt::runtime(
            "{{\n id: {}\n name: {}\n image: {}\n status: {}\n ports: {}\n mounts: {}\n devices: {}\n env: {}\n}}"),
        container.id,
        container.name,
        container.image,
        (int)container.status,
        container.ports,
        container.mounts,
        container.devices,
        container.env);
  }
};
} // namespace fmt