#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

/** https://github.com/whot/libevdev/blob/c3953e1bb8f813f3e248047d23fb0775b147da9f/libevdev/libevdev-uinput.c#L41 **/
#define SYS_INPUT_DIR "/sys/devices/virtual/input/"

#define LOG(Thing) std::cout << "[fake-uinput] " << Thing << std::endl;

typedef int (*ioctl_t)(int, unsigned long, ...);
static ioctl_t real_ioctl = nullptr;

bool load_library() {
  LOG("Loading ...")
  // Load the real ioctl
  real_ioctl = reinterpret_cast<ioctl_t>(dlsym(RTLD_NEXT, "ioctl"));
  if (!real_ioctl) {
    LOG("Error: " << dlerror());
    return false;
  }
  return true;
}

void mount(const std::filesystem::path &sysfs_path, const std::string &filename) {
  std::filesystem::path path(filename);
  if (!std::filesystem::exists(path)) {
    // Get the major and minor numbers of the device
    // They are handily available in the sysfs directory
    std::ifstream major_minor_file(sysfs_path / "dev");
    unsigned int major, minor;
    char sep;
    major_minor_file >> major >> sep >> minor;

    int rc = mknod(filename.c_str(), S_IFCHR | 0666, makedev(major, minor));
    if (rc == -1) {
      LOG("Error creating device node: " << strerror(errno));
    } else {
      LOG("Created device node: " << filename);
    }
  }
}

/**
 * glibc, BSD
 * TODO: musl uses int op instead of unsigned long
 */
extern "C" int ioctl(int fd, unsigned long request, ...) {
  if (!real_ioctl) {
    if (!load_library()) {
      LOG("Error: real_ioctl is not initialized");
      return -1;
    }
  }

  // Call the real ioctl
  va_list args;
  va_start(args, request);
  void *arg = va_arg(args, void *);
  va_end(args);
  int result = real_ioctl(fd, request, arg);

  if (result >= 0 && request == UI_DEV_CREATE) { // Creating a new uinput device
    LOG("Intercepted UI_DEV_CREATE ioctl call");

    // Get the sysfs name of the created uinput device
    char buf[sizeof(SYS_INPUT_DIR) + 64] = SYS_INPUT_DIR;
    auto rc = real_ioctl(fd, UI_GET_SYSNAME(sizeof(buf) - strlen(SYS_INPUT_DIR)), &buf[strlen(SYS_INPUT_DIR)]);
    if (rc != -1) {
      // iterate over the files under /sys/devices/virtual/input/ and find "eventX" and "jsX" files
      for (auto &p : std::filesystem::directory_iterator(buf)) {
        if (p.is_directory()) {
          auto path = p.path();
          auto filename = path.filename().string();
          if (filename.find("event") != std::string::npos) {
            mount(path, "/dev/input/" + filename);
          } else if (filename.find("js") != std::string::npos) {
            mount(path, "/dev/input/" + filename);
          }
        }
      }

      // TODO: udev? using /sys/devices/virtual/input/{buf}/uevent ?

    } else {
      LOG("Error getting sysname: " << strerror(errno));
    }
  }

  // return the original result
  return result;
}
