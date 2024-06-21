#pragma once

#include <fcntl.h>
#include <helpers/logger.hpp>
#include <inputtino/protected_types.hpp>
#include <libinput.h>
#include <memory>
#include <platforms/linux/uinput/uinput.hpp>
#include <string>
#include <unistd.h>
#include <vector>

static int open_restricted(const char *path, int flags, void *user_data) {
  int fd = open(path, flags);
  return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
  close(fd);
}

const static struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void LogHandler(libinput __attribute__((unused)) * libinput,
                       libinput_log_priority priority,
                       const char *format,
                       va_list args) {
  if (priority == LIBINPUT_LOG_PRIORITY_DEBUG) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), format, args);
    if (n > 0)
      logs::log(logs::debug, "libinput: {}", buf);
  }
}

static std::shared_ptr<libinput> create_libinput_context(const std::vector<std::string> &nodes) {
  auto li = libinput_path_create_context(&interface, NULL);
  libinput_log_set_handler(li, LogHandler);
  libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
  for (const auto &node : nodes) {
    libinput_path_add_device(li, node.c_str());
  }
  return std::shared_ptr<libinput>(li, [](libinput *li) { libinput_unref(li); });
}

static std::shared_ptr<libinput_event> get_event(std::shared_ptr<libinput> li) {
  libinput_dispatch(li.get());
  struct libinput_event *event = libinput_get_event(li.get());
  return std::shared_ptr<libinput_event>(event, [](libinput_event *event) { libinput_event_destroy(event); });
}

static void link_devnode(libevdev *dev, const std::string &device_node) {
  // We have to sleep in order to be able to read from the newly created device
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto fd = open(device_node.c_str(), O_RDONLY | O_NONBLOCK);
  assert(fd >= 0 && "Unable to open device node");
  libevdev_set_fd(dev, fd);
}

static std::vector<inputtino::libevdev_event_ptr> fetch_events_debug(const wolf::core::input::libevdev_ptr &dev,
                                                                     int max_events = 50) {
  auto events = wolf::core::input::fetch_events(dev, max_events);
  for (auto event : events) {
    logs::log(logs::debug,
              "Event: type={}, code={}, value={}",
              libevdev_event_type_get_name(event->type),
              libevdev_event_code_get_name(event->type, event->code),
              event->value);
  }
  return events;
}