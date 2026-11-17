#include "mount.hpp"

std::error_code bind_node(const std::filesystem::path &source, const std::filesystem::path &target) {
  return std::make_error_code(std::errc::function_not_supported);
}

std::error_code unbind_node(const std::filesystem::path &target) {
  return std::make_error_code(std::errc::function_not_supported);
}
