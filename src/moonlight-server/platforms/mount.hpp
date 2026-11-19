#pragma once

#include <filesystem>
#include <system_error>

/**
 * Bind mounts the file `source` over the file `target`
 */
std::error_code bind_node(const std::filesystem::path &source, const std::filesystem::path &target);

/**
 * Lazily detaches the topmost mount at `target`, a held file descriptor keeps working until closed.
 * Fails with EINVAL when `target` is not a mount point.
 */
std::error_code unbind_node(const std::filesystem::path &target);
