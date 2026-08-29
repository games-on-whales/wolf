#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
#include <xf86drm.h>

std::shared_ptr<drmDevice> drm_open_device(std::string_view device);

namespace {
int lookup_result = 0;
int lookup_count = 0;
int opened_fd = -1;
int free_count = 0;

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool is_open(int fd) {
  return fcntl(fd, F_GETFD) != -1;
}
} // namespace

// Only libdrm is replaced. The production owner and POSIX descriptors run for real.
extern "C" int drmGetDevice2(int fd, uint32_t, drmDevicePtr *device) {
  opened_fd = fd;
  ++lookup_count;
  if (lookup_result < 0) {
    return lookup_result;
  }
  *device = new drmDevice{};
  return 0;
}

extern "C" void drmFreeDevice(drmDevicePtr *device) {
  ++free_count;
  delete *device;
  *device = nullptr;
}

int main() {
  try {
    const int unrelated = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    require(unrelated >= 0, "cannot open unrelated descriptor");
    auto device = drm_open_device("/dev/null");
    const int owned = opened_fd;
    require(owned >= 0 && owned != unrelated && is_open(owned), "device must own an open descriptor");
    auto retained = device;
    device.reset();
    require(is_open(owned) && free_count == 0, "a retained device must remain open");
    retained.reset();
    require(!is_open(owned) && errno == EBADF, "last owner must close the acquired descriptor");
    require(is_open(unrelated) && free_count == 1, "cleanup must not close another descriptor");

    lookup_result = -EINVAL;
    bool failed = false;
    try {
      drm_open_device("/dev/null");
    } catch (const std::runtime_error &) {
      failed = true;
    }
    require(failed && !is_open(opened_fd), "failed lookup must close its acquired descriptor");
    require(is_open(unrelated) && free_count == 1, "failed lookup must not release another device");

    const int prior_lookups = lookup_count;
    failed = false;
    try {
      drm_open_device("/dev/null/not-a-device");
    } catch (const std::runtime_error &) {
      failed = true;
    }
    require(failed && lookup_count == prior_lookups, "failed open must not call libdrm");

    lookup_result = 0;
    const int saved_stdin = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3);
    close(STDIN_FILENO);
    device = drm_open_device("/dev/null");
    require(opened_fd == 0 && is_open(0), "descriptor zero is a valid owned descriptor");
    device.reset();
    require(!is_open(0) && is_open(unrelated), "descriptor zero must close without affecting another descriptor");
    if (saved_stdin >= 0) {
      require(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO, "cannot restore stdin");
      close(saved_stdin);
    }
    close(unrelated);
    std::cout << "DRM descriptor ownership checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
