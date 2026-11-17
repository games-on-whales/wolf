#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace wolf::core::docker {

constexpr std::string_view ROOTLESS_INPUT_MOUNT = "/dev/input";

/**
 * @brief true when WOLF_ROOTLESS=TRUE, Wolf is talking to a rootless container engine
 */
bool is_rootless();

/**
 * @brief Adapts the container create options to a rootless engine:
 * drops `HostConfig.DeviceCgroupRules`, adds `label=disable` to `SecurityOpt`
 * (unless the options already carry a `label=` entry),
 * sets `HostConfig.OomScoreAdj` to `oom_score_adj` (unless the options already set it)
 */
std::string apply_rootless_hostconfig(std::string base_create_json, int oom_score_adj);

/**
 * @brief Wolf's own `/proc/self/oom_score_adj`, never below 0 (0 when unreadable)
 * A container exec under a rootless user manager fails when the container's score is lower than the manager's
 */
int own_oom_score_adj();

struct InputBindPlan {
  std::filesystem::path source; // the device node on the host, as seen by Wolf
  std::filesystem::path target; // the file in the per-session input dir that the node gets bound to
  std::string link;             // when not empty: the path in the container that must link to the bound node
};

/**
 * @brief Where a device node ends up in the per-session input dir
 * Only nodes under /dev/input and /dev/hidraw* are accepted, anything else (or any `..`) yields nullopt
 */
std::optional<InputBindPlan> plan_input_bind(const std::filesystem::path &input_dir, std::string_view devname);

/**
 * @brief The shell command run in the container when a device node is plugged in
 */
std::string plug_command(std::string_view udev_msg, const InputBindPlan &plan);

/**
 * @brief The shell command run in the container when a device node is unplugged
 */
std::string unplug_command(std::string_view udev_msg, const InputBindPlan &plan);

/**
 * @brief Detaches whatever a previous run left in `input_dir` and creates it empty
 */
void prepare_input_dir(const std::filesystem::path &input_dir);

/**
 * @brief The device nodes bound by one run of a container
 * The mutex serialises the mount and umount syscalls, which can come from several threads (udev events, teardown)
 */
class InputBinds {
public:
  bool plug(const InputBindPlan &plan);
  void unplug(const InputBindPlan &plan);
  void release_all();

private:
  std::mutex mutex;
  std::set<std::filesystem::path> targets;
};

} // namespace wolf::core::docker
