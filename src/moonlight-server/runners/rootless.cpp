#include <algorithm>
#include <boost/json.hpp>
#include <fmt/core.h>
#include <fstream>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <platforms/mount.hpp>
#include <runners/rootless.hpp>

namespace wolf::core::docker {

namespace json = boost::json;

bool is_rootless() {
  auto env = utils::get_env("WOLF_ROOTLESS");
  return env && std::string_view(env) == "TRUE";
}

int own_oom_score_adj() {
  std::ifstream in("/proc/self/oom_score_adj");
  int value = 0;
  if (!(in >> value)) {
    return 0;
  }
  return std::max(value, 0);
}

std::string apply_rootless_hostconfig(std::string base_create_json, int oom_score_adj) {
  auto parsed = utils::parse_json(base_create_json).as_object();

  json::object host_config;
  if (auto existing = parsed.if_contains("HostConfig"); existing && existing->is_object()) {
    host_config = existing->as_object();
  }
  host_config.erase("DeviceCgroupRules");

  json::array security_opt;
  if (auto existing = host_config.if_contains("SecurityOpt"); existing && existing->is_array()) {
    security_opt = existing->as_array();
  }
  bool has_label = std::any_of(security_opt.begin(), security_opt.end(), [](const json::value &opt) {
    return opt.is_string() && opt.as_string().starts_with("label=");
  });
  if (!has_label) {
    security_opt.push_back("label=disable");
  }
  host_config["SecurityOpt"] = std::move(security_opt);

  if (auto existing = host_config.if_contains("OomScoreAdj"); !existing || !existing->is_number()) {
    host_config["OomScoreAdj"] = oom_score_adj;
  }

  parsed["HostConfig"] = std::move(host_config);
  return json::serialize(parsed);
}

std::optional<InputBindPlan> plan_input_bind(const std::filesystem::path &input_dir, std::string_view devname) {
  std::filesystem::path source(devname);
  if (!source.is_absolute() || !source.has_filename()) {
    return std::nullopt;
  }
  for (const auto &part : source) {
    if (part == "..") {
      return std::nullopt;
    }
  }

  auto name = source.filename().string();
  auto parent = source.parent_path();
  InputBindPlan plan = {.source = source, .target = input_dir / name};
  if (parent == ROOTLESS_INPUT_MOUNT) {
    return plan;
  }
  if (parent == "/dev" && name.starts_with("hidraw")) {
    plan.link = source.string();
    return plan;
  }
  return std::nullopt;
}

std::string plug_command(std::string_view udev_msg, const InputBindPlan &plan) {
  if (plan.link.empty()) {
    return fmt::format("fake-udev -m {}", udev_msg);
  }
  return fmt::format("ln -sf {}/{} {} && fake-udev -m {}",
                     ROOTLESS_INPUT_MOUNT,
                     plan.target.filename().string(),
                     plan.link,
                     udev_msg);
}

std::string unplug_command(std::string_view udev_msg, const InputBindPlan &plan) {
  if (plan.link.empty()) {
    return fmt::format("fake-udev -m {}", udev_msg);
  }
  return fmt::format("fake-udev -m {} && rm -f {}", udev_msg, plan.link);
}

/**
 * Stacked binds (a replug that was never detached) come off one at a time, EINVAL means nothing is left.
 */
static void detach_all(const std::filesystem::path &target) {
  while (!unbind_node(target)) {
  }
}

void prepare_input_dir(const std::filesystem::path &input_dir) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(input_dir, ec)) {
    detach_all(entry.path());
  }
  std::filesystem::remove_all(input_dir, ec);
  std::filesystem::create_directories(input_dir, ec);
  if (ec) {
    logs::log(logs::warning, "[DOCKER] Unable to create {}: {}", input_dir.string(), ec.message());
  }
}

bool InputBinds::plug(const InputBindPlan &plan) {
  std::lock_guard lock(mutex);
  detach_all(plan.target);
  if (!std::ofstream(plan.target)) {
    logs::log(logs::warning, "[DOCKER] Unable to create {}", plan.target.string());
    return false;
  }
  if (auto ec = bind_node(plan.source, plan.target)) {
    // A node that vanished before it was bound (short-lived devices at session start) is not an error
    logs::log(ec == std::errc::no_such_file_or_directory ? logs::debug : logs::warning,
              "[DOCKER] Unable to bind {} to {}: {}",
              plan.source.string(),
              plan.target.string(),
              ec.message());
    std::error_code rm_ec;
    std::filesystem::remove(plan.target, rm_ec);
    return false;
  }
  targets.insert(plan.target);
  return true;
}

void InputBinds::unplug(const InputBindPlan &plan) {
  std::lock_guard lock(mutex);
  detach_all(plan.target);
  std::error_code ec;
  std::filesystem::remove(plan.target, ec);
  targets.erase(plan.target);
}

void InputBinds::release_all() {
  std::lock_guard lock(mutex);
  for (const auto &target : targets) {
    detach_all(target);
    std::error_code ec;
    std::filesystem::remove(target, ec);
  }
  targets.clear();
}

} // namespace wolf::core::docker
