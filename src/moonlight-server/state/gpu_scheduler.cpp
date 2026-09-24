#include <state/gpu_scheduler.hpp>

#include <algorithm>
#include <filesystem>

#include <helpers/logger.hpp>

namespace state {

namespace {
std::vector<wolf::config::GPUConfig> discover_gpus() {
  std::vector<wolf::config::GPUConfig> result;
  std::error_code ec;
  const std::filesystem::path drm{ "/dev/dri" };
  if (!std::filesystem::exists(drm, ec)) {
    return result;
  }
  for (const auto &entry : std::filesystem::directory_iterator(drm, ec)) {
    const auto name = entry.path().filename().string();
    if (name.rfind("renderD", 0) == 0) {
      result.push_back({entry.path().string(), 1});
    }
  }
  std::sort(result.begin(), result.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.render_node < rhs.render_node;
  });
  return result;
}
} // namespace

GPUScheduler::GPUScheduler(std::vector<wolf::config::GPUConfig> configured, std::vector<std::string> excluded) {
  if (configured.empty()) {
    configured = discover_gpus();
  }
  std::sort(configured.begin(), configured.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.render_node < rhs.render_node;
  });
  for (const auto &gpu : configured) {
    if (std::find(excluded.begin(), excluded.end(), gpu.render_node) != excluded.end()) {
      continue;
    }
    if (gpu.render_node.empty() || gpu.weight <= 0) {
      logs::log(logs::warning, "Ignoring invalid GPU configuration: node='{}', weight={}", gpu.render_node, gpu.weight);
      continue;
    }
    if (std::none_of(gpus_.begin(), gpus_.end(), [&](const auto &existing) {
          return existing.render_node == gpu.render_node;
        })) {
      gpus_.push_back({gpu.render_node, gpu.weight});
    }
  }
}

std::optional<GPUAssignment> GPUScheduler::acquire() {
  std::scoped_lock lock(mutex_);
  if (gpus_.empty()) {
    return std::nullopt;
  }
  auto best = std::min_element(gpus_.begin(), gpus_.end(), [](const auto &lhs, const auto &rhs) {
    // Compare the load after assignment so a higher weight wins when both GPUs are idle.
    // Cross multiplication avoids floating-point tie instability.
    const auto lhs_score = (lhs.active + 1) * static_cast<std::size_t>(rhs.weight);
    const auto rhs_score = (rhs.active + 1) * static_cast<std::size_t>(lhs.weight);
    if (lhs_score != rhs_score) {
      return lhs_score < rhs_score;
    }
    return lhs.render_node < rhs.render_node;
  });
  const auto slot = static_cast<std::size_t>(std::distance(gpus_.begin(), best));
  ++best->active;
  const auto token = next_token_++;
  active_assignments_.emplace(token, slot);
  logs::log(logs::debug, "Assigned session to GPU {} (active={}, weight={})", best->render_node, best->active, best->weight);
  return GPUAssignment{best->render_node, slot, token};
}

void GPUScheduler::release(const GPUAssignment &assignment) {
  std::scoped_lock lock(mutex_);
  auto active = active_assignments_.find(assignment.token);
  if (active == active_assignments_.end()) {
    return;
  }
  const auto slot = active->second;
  active_assignments_.erase(active);
  if (slot >= gpus_.size() || gpus_[slot].render_node != assignment.render_node) {
    return;
  }
  if (gpus_[slot].active > 0) {
    --gpus_[slot].active;
  }
}

std::vector<std::pair<std::string, std::size_t>> GPUScheduler::loads() const {
  std::scoped_lock lock(mutex_);
  std::vector<std::pair<std::string, std::size_t>> result;
  for (const auto &gpu : gpus_) {
    result.emplace_back(gpu.render_node, gpu.active);
  }
  return result;
}

} // namespace state
