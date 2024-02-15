#pragma once
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wolf::core::docker {
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
 * CURL needs to be initialised once
 */
void init();

class DockerAPI {
private:
  std::string socket_path; // TODO: add B64 registry_auth

public:
  inline explicit DockerAPI(std::string socket_path = "/var/run/docker.sock") : socket_path(std::move(socket_path)) {}

  /**
   * Get a list of all containers
   *
   * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerList
   * @param all: Return all containers. If false, only running containers are shown
   */
  [[nodiscard]] std::vector<Container> get_containers(bool all = true) const;

  /**
   * Get a container
   *
   * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerInspect
   */
  [[nodiscard]] std::optional<Container> get_by_id(std::string_view id) const;

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
                                  bool force_recreate_if_present = true) const;

  /**
   * Starts the container
   *
   * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerStart
   */
  bool start_by_id(std::string_view id) const;

  /**
   * Stops the container
   *
   * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerStop
   * @param timeout_seconds: Number of seconds to wait before killing the container
   */
  bool stop_by_id(std::string_view id, int timeout_seconds = 2) const;

  /**
   * Removes the container
   *
   * https://docs.docker.com/engine/api/v1.30/#tag/Container/operation/ContainerDelete
   *
   * @param remove_volumes: Remove anonymous volumes associated with the container.
   * @param force: If the container is running, kill it before removing it.
   * @param link: Remove the specified link associated with the container.
   */
  bool remove_by_id(std::string_view id, bool remove_volumes = false, bool force = false, bool link = false) const;

  /**
   * Searches for a container with the given name and then removes it if present.
   */
  bool remove_by_name(std::string_view name, bool remove_volumes = false, bool force = false, bool link = false) const;

  /**
   * Downloads a Docker image
   */
  bool pull_image(std::string_view image_name, std::string_view registry_auth = {}) const;

  bool exec(std::string_view id, const std::vector<std::string_view> &command, std::string_view user = "root") const;

  /**
   * Get the container logs
   * https://docs.docker.com/engine/api/v1.40/#tag/Container/operation/ContainerLogs
   */
  std::string get_logs(std::string_view id,
                       bool get_stdout = true,
                       bool get_stderr = true,
                       int since = 0,
                       int until = 0,
                       bool timestamps = false);
};

} // namespace wolf::core::docker