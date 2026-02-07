#include "uinput.hpp"
#include <helpers/logger.hpp>

namespace wolf::core::input {

std::vector<inputtino::libevdev_event_ptr> fetch_events(const libevdev_ptr &dev, int max_events) {
  std::vector<inputtino::libevdev_event_ptr> events = {};
  input_event evt = {};
  int read_events = 1;
  int ret = libevdev_next_event(dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &evt);
  if (ret == LIBEVDEV_READ_STATUS_SUCCESS) { // In normal mode libevdev_next_event returns SUCCESS and returns the event
    events.push_back(std::make_shared<input_event>(evt));
  }
  // If the current event is an EV_SYN SYN_DROPPED event, libevdev_next_event returns LIBEVDEV_READ_STATUS_SYNC and ev
  // is set to the EV_SYN event The caller should now call this function with the LIBEVDEV_READ_FLAG_SYNC flag set, to
  // get the set of events that make up the device state delta libevdev_next_event returns LIBEVDEV_READ_STATUS_SYNC for
  // each event part of that delta, until it returns -EAGAIN once all events have been synced
  while (ret >= 0 && read_events < max_events) {
    if (ret == LIBEVDEV_READ_STATUS_SYNC) {
      ret = libevdev_next_event(dev.get(), LIBEVDEV_READ_FLAG_SYNC, &evt);
    } else if (ret == LIBEVDEV_READ_STATUS_SUCCESS) {
      ret = libevdev_next_event(dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &evt);
    }
    if (evt.type != EV_SYN) { // We want to return all events apart from EV_SYN
      events.push_back(std::make_shared<input_event>(evt));
    }
    read_events++;
  }
  return events;
}

std::vector<inputtino::libevdev_event_ptr> fetch_events(int uinput_fd, int max_events) {
  std::vector<inputtino::libevdev_event_ptr> events = {};
  struct input_event ev {};
  int ret, read_events = 0;
  while (read_events < max_events && (ret = read(uinput_fd, &ev, sizeof(ev))) == sizeof(ev)) {
    events.push_back(std::make_shared<input_event>(ev));
    read_events++;
  }
  if (ret < 0 && errno != EAGAIN) {
    logs::log(logs::warning, "Failed reading uinput fd; ret={}", strerror(errno));
  } else if (ret > 0) {
    logs::log(logs::warning, "Uinput incorrect read size of {}", ret);
  }

  return events;
}

} // namespace wolf::core::input
