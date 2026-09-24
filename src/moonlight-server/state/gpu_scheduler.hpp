#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <state/serialised_config.hpp>

namespace state {

struct GPUAssignment {
  std::string render_node;
  std::size_t slot = 0;
  std::uint64_t token = 0;
};

/** Thread-safe weighted least-loaded scheduler for per-session GPU assignments. */
class GPUScheduler {
public:
  explicit GPUScheduler(std::vector<wolf::config::GPUConfig> configured = {},
                        std::vector<std::string> excluded = {});

  std::optional<GPUAssignment> acquire();
  void release(const GPUAssignment &assignment);
  std::vector<std::pair<std::string, std::size_t>> loads() const;

private:
  struct GPU {
    std::string render_node;
    int weight;
    std::size_t active = 0;
  };

  mutable std::mutex mutex_;
  std::vector<GPU> gpus_;
  std::uint64_t next_token_ = 1;
  std::unordered_map<std::uint64_t, std::size_t> active_assignments_;
};

} // namespace state
