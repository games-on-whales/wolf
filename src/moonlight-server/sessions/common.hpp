#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>

#include <helpers/logger.hpp>
#include <helpers/utils.hpp>

namespace wolf::core::sessions {

constexpr std::string_view VIRTUAL_SINK_PREFIX = "virtual_sink_";
constexpr std::chrono::milliseconds DEFAULT_WAYLAND_SOCKET_WAIT_TIMEOUT = std::chrono::seconds(5);

inline std::chrono::milliseconds get_wayland_socket_wait_timeout() {
  auto timeout_ms = utils::get_env("WOLF_WAYLAND_SOCKET_WAIT_TIMEOUT_MS");
  if (timeout_ms) {
    try {
      auto parsed = std::stoll(timeout_ms);
      if (parsed >= 0) {
        return std::chrono::milliseconds(parsed);
      }
      logs::log(logs::warning,
                "Ignoring negative WOLF_WAYLAND_SOCKET_WAIT_TIMEOUT_MS={}, using default {}ms",
                timeout_ms,
                DEFAULT_WAYLAND_SOCKET_WAIT_TIMEOUT.count());
    } catch (const std::exception &) {
      logs::log(logs::warning,
                "Ignoring invalid WOLF_WAYLAND_SOCKET_WAIT_TIMEOUT_MS={}, using default {}ms",
                timeout_ms,
                DEFAULT_WAYLAND_SOCKET_WAIT_TIMEOUT.count());
    }
  }

  return DEFAULT_WAYLAND_SOCKET_WAIT_TIMEOUT;
}

inline bool wait_for_wayland_socket(std::string_view runtime_dir,
                                    const std::string &socket_name,
                                    std::chrono::milliseconds timeout = get_wayland_socket_wait_timeout()) {
  if (auto skip_wait = utils::get_env("WOLF_SKIP_WAYLAND_SOCKET_WAIT")) {
    if (std::string_view(skip_wait) == "TRUE" || std::string_view(skip_wait) == "1") {
      return true;
    }
  }

  auto socket_path = std::filesystem::path(runtime_dir) / socket_name;
  auto deadline = std::chrono::steady_clock::now() + timeout;
  struct stat st {};

  while (std::chrono::steady_clock::now() < deadline) {
    if (stat(socket_path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) {
      // The compositor creates the socket 0755 root:root, and connect(2) wants
      // write permission: an app container running as anything but root — every
      // container that sets `User` — cannot reach the display it was started
      // for. The audio socket next to it is already world-writable for the same
      // reason.
      std::error_code perms_err;
      std::filesystem::permissions(socket_path, std::filesystem::perms::all, perms_err);
      if (perms_err) {
        logs::log(logs::warning,
                  "Unable to make Wayland socket {} world-writable ({}); non-root apps will not connect",
                  socket_path.string(),
                  perms_err.message());
      }
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (stat(socket_path.c_str(), &st) == 0) {
    logs::log(logs::error,
              "Wayland endpoint {} exists but is not a socket (mode={:o})",
              socket_path.string(),
              st.st_mode);
  } else {
    logs::log(logs::error, "Wayland socket {} was never created", socket_path.string());
  }

  return false;
}
} // namespace wolf::core::sessions
