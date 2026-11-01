#include <filesystem>
#include <fstream>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <introspect/introspect.hpp>
#include <regex>

namespace introspect {

std::optional<wolf::core::docker::Container> get_current_container(const wolf::core::docker::DockerAPI &docker_api) {
  // The trick here is to look for the mount points under /proc/self/mountinfo
  // With Docker, you'll get something like:
  // /var/lib/docker/containers/f8b41e030e791cf45c52855fa310b86ac32cae06d8c4474a2903fc0a9ce3b926/hostname
  // where `f8b41e030e791cf45c52855fa310b86ac32cae06d8c4474a2903fc0a9ce3b926` is the actual container ID

  std::ifstream mountinfo("/proc/self/mountinfo");
  if (!mountinfo.is_open()) {
    logs::log(logs::warning, "Failed to open /proc/self/mountinfo");
    return std::nullopt;
  }

  // Look for HEX strings of at least 64 character in size that ends with `/host`
  // (we should capture `/hosts` and `/hostname` this way)
  std::regex container_id_pattern("/([a-fA-F0-9]{64,})/.*/host");
  std::string line;
  while (std::getline(mountinfo, line)) {
    std::smatch matches;
    if (std::regex_search(line, matches, container_id_pattern) && matches.size() > 1) {
      std::string container_id = matches[1].str();
      if (auto container = docker_api.get_by_id(container_id)) {
        logs::log(logs::debug, "We are running as container ID {}", container_id);
        return container;
      } else {
        logs::log(logs::debug, "Unable to find a valid Docker container ID in {}, checked {}", line, container_id);
      }
    }
  }

  logs::log(logs::warning, "No valid container ID found in mountinfo, are we running in Docker?");

  return std::nullopt;
}

std::optional<std::string> get_host_path_for(const wolf::core::docker::Container &container,
                                             std::string_view local_path) {
  // return the first mount that matches local_path as the target
  for (const auto &m : container.mounts) {
    if (m.destination.find(local_path) != std::string::npos) {
      // found a partial match, we have to check that the path is actually the same.
      // for example if destination is `/etc/wolf/cfg/` and local_path is `/etc/wolf/` we should discard this
      if (std::filesystem::path(local_path) == std::filesystem::path(m.destination)) {
        logs::log(logs::info, "Detected mounted {} in the host as {}", local_path, m.source);
        return m.source;
      } else {
        logs::log(logs::debug, "Found partial match but {} != {}", local_path, m.destination);
      }
    }
  }
  logs::log(logs::error, "Unable to find docker mount for path: {}", local_path);

  return std::nullopt;
}
} // namespace introspect