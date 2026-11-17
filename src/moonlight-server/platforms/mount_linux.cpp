#include "mount.hpp"
#include <cerrno>
#include <sys/mount.h>

std::error_code bind_node(const std::filesystem::path &source, const std::filesystem::path &target) {
  if (::mount(source.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) != 0) {
    return {errno, std::system_category()};
  }
  return {};
}

std::error_code unbind_node(const std::filesystem::path &target) {
  if (::umount2(target.c_str(), MNT_DETACH) != 0) {
    return {errno, std::system_category()};
  }
  return {};
}
