#pragma once
#include <optional>

namespace docker {
constexpr auto DOCKER_API_VERSION = "v1.40";

enum ContainerStatus {
  CREATED,
  RUNNING,
  PAUSED,
  RESTARTING,
  REMOVING,
  EXITED,
  DEAD
};

enum PortType {
  TCP,
  UDP
};

struct Port {
  int private_port;
  int public_port;
  PortType type;
};

struct MountPoint {
  std::string source;
  std::string destination;
  std::string mode;
};

struct Device {
  std::string path_on_host;
  std::string path_in_container;
  std::string cgroup_permission;
};

struct Container {
  std::string id;
  std::string name;

  std::string image;

  ContainerStatus status;

  std::vector<Port> ports;
  std::vector<MountPoint> mounts;
  std::vector<Device> devices;
  std::vector<std::string> env;
};

/**
 *
 */
void init();

/**
 * Get a list of all containers
 *
 * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerList
 * @param all: Return all containers. If false, only running containers are shown
 */
std::vector<Container> get_containers(bool all = true);

/**
 * Get a container
 *
 * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerInspect
 */
std::optional<Container> get_by_id(std::string_view id);

/**
 * On success, returns the newly created docker container
 * this will differ from the input container, for example:
 *  - `id` will be added based on the returned ID
 *  - `env` will be the merged with the original container ENV variables
 *
 *  https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerCreate
 *
 *  @param registry_auth: optional, base64 encoded registry auth in case the image is missing
 *  @see https://docs.docker.com/engine/api/v1.30/#section/Authentication
 *
 *  @param force_recreate_if_present: if a container with the same name is already present it will be removed
 */
std::optional<Container> create(const Container &container,
                                std::string_view custom_params = "{}",
                                std::string_view registry_auth = {},
                                bool force_recreate_if_present = true);

/**
 * Starts the container
 *
 * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerStart
 */
bool start_by_id(std::string_view id);

/**
 * Stops the container
 *
 * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerStop
 * @param timeout_seconds: Number of seconds to wait before killing the container
 */
bool stop_by_id(std::string_view id, int timeout_seconds = 2);

/**
 * Removes the container
 *
 * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerDelete
 *
 * @param remove_volumes: Remove anonymous volumes associated with the container.
 * @param force: If the container is running, kill it before removing it.
 * @param link: Remove the specified link associated with the container.
 */
bool remove_by_id(std::string_view id, bool remove_volumes = false, bool force = false, bool link = false);

/**
 * Searches for a container with the given name and then removes it if present.
 */
bool remove_by_name(std::string_view name, bool remove_volumes = false, bool force = false, bool link = false);

/**
 * Downloads a Docker image
 */
bool pull_image(std::string_view image_name, std::string_view registry_auth = {});

} // namespace docker