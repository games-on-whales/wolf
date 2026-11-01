#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace utils {

std::optional<std::string> curl_get(std::string_view url);

std::optional<std::string> get_file_content(const std::filesystem::path &path);

/**
 * Returns the raw content of the icon (if available).
 *
 * icon_path can be a URL, a relative or absolute path
 */
std::optional<std::string> get_icon(std::string_view base_local_path, std::string_view icon_path);

} // namespace utils