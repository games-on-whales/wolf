#pragma once

#include <core/docker.hpp>

namespace introspect {

/**
 * Returns the Docker container currently running this program (if any)
 */
std::optional<wolf::core::docker::Container> get_current_container(const wolf::core::docker::DockerAPI &docker_api);

/**
 * Resolves the host path corresponding to a given local path (if externally mounted).
 */
std::optional<std::string> get_host_path_for(const wolf::core::docker::Container &container,
                                             std::string_view local_path);

} // namespace introspect