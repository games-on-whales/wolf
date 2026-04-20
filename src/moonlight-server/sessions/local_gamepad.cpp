#include <sessions/handlers.hpp>

#include <core/input.hpp>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <state/sessions.hpp>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace wolf::core::sessions {

namespace {

using namespace std::chrono_literals;

constexpr auto JOIN_HOLD_DURATION = 3s;

struct WatchedGamepad {
  std::string devnode;
  int fd = -1;
  std::shared_ptr<libevdev> dev;
  bool select_down = false;
  bool start_down = false;
  bool chord_triggered = false;
  bool grabbed = false;
  std::optional<std::chrono::steady_clock::time_point> both_pressed_since = std::nullopt;
};

std::shared_ptr<libevdev> make_libevdev_ptr(libevdev *dev) {
  return std::shared_ptr<libevdev>(dev, [](libevdev *ptr) {
    if (ptr) {
      libevdev_free(ptr);
    }
  });
}

std::pair<unsigned int, unsigned int> get_major_minor(const std::string &devnode) {
  struct stat buf {};
  if (stat(devnode.c_str(), &buf) == -1 || !S_ISCHR(buf.st_mode)) {
    return {};
  }
  return {major(buf.st_rdev), minor(buf.st_rdev)};
}

std::string gen_udev_hw_db_filename(const std::string &devnode) {
  auto [dev_major, dev_minor] = get_major_minor(devnode);
  return fmt::format("c{}:{}", dev_major, dev_minor);
}

std::map<std::string, std::string>
gen_udev_base_event(const std::string &devnode, const std::string &syspath, const std::string &action = "add") {
  auto [dev_major, dev_minor] = get_major_minor(devnode);
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return {
      {"ACTION", action},
      {"SEQNUM", "7"},
      {"USEC_INITIALIZED", std::to_string(timestamp)},
      {"SUBSYSTEM", "input"},
      {"ID_INPUT", "1"},
      {"ID_SERIAL", "noserial"},
      {"TAGS", ":seat:uaccess:"},
      {"CURRENT_TAGS", ":seat:uaccess:"},
      {"DEVNAME", devnode},
      {"DEVPATH", syspath},
      {"MAJOR", std::to_string(dev_major)},
      {"MINOR", std::to_string(dev_minor)},
  };
}

void close_watcher(WatchedGamepad &watcher) {
  if (watcher.dev && watcher.grabbed) {
    libevdev_grab(watcher.dev.get(), LIBEVDEV_UNGRAB);
  }
  watcher.grabbed = false;
  watcher.dev.reset();
  if (watcher.fd >= 0) {
    close(watcher.fd);
    watcher.fd = -1;
  }
}

std::optional<std::string> syspath_for_devnode(const std::string &devnode) {
  try {
    auto syspath = std::filesystem::canonical(std::filesystem::path("/sys/class/input") /
                                              std::filesystem::path(devnode).filename());
    auto syspath_str = syspath.string();
    if (syspath_str.rfind("/sys", 0) == 0) {
      syspath_str.erase(0, 4);
    }
    return syspath_str;
  } catch (const std::filesystem::filesystem_error &) {
    return std::nullopt;
  }
}

std::string bus_type_to_hwdb(unsigned int bustype) {
  switch (bustype) {
  case BUS_USB:
    return "usb";
  case BUS_BLUETOOTH:
    return "bluetooth";
  default:
    return "unknown";
  }
}

std::optional<state::PhysicalInputDeviceAssignment>
inspect_gamepad_assignment(const std::string &devnode, libevdev *dev, std::string owner_session_id) {
  auto event_syspath = syspath_for_devnode(devnode);
  if (!event_syspath) {
    return std::nullopt;
  }

  state::PhysicalInputDeviceAssignment assignment{
      .devnode = devnode,
      .owner_session_id = std::move(owner_session_id),
      .routed_session_id = assignment.owner_session_id,
      .stop_session_on_disconnect = true,
  };

  auto event = gen_udev_base_event(devnode, *event_syspath);
  event["ID_INPUT_JOYSTICK"] = "1";
  event[".INPUT_CLASS"] = "joystick";
  assignment.udev_events.push_back(event);

  std::vector<std::string> hwdb_rows = {
      "E:ID_INPUT=1",
      "E:ID_INPUT_JOYSTICK=1",
      fmt::format("E:ID_BUS={}", bus_type_to_hwdb(libevdev_get_id_bustype(dev))),
      "G:seat",
      "G:uaccess",
      "Q:seat",
      "Q:uaccess",
      "V:1",
  };
  assignment.udev_hw_db_entries.push_back({gen_udev_hw_db_filename(devnode), hwdb_rows});

  try {
    auto device_dir = std::filesystem::canonical(std::filesystem::path("/sys/class/input") /
                                                 std::filesystem::path(devnode).filename() / "device");
    for (const auto &entry : std::filesystem::directory_iterator(device_dir)) {
      auto name = entry.path().filename().string();
      if (!entry.is_directory() || name.rfind("js", 0) != 0) {
        continue;
      }

      auto js_devnode = (std::filesystem::path("/dev/input") / name).string();
      auto js_syspath = syspath_for_devnode(js_devnode);
      if (!js_syspath) {
        continue;
      }

      auto js_event = gen_udev_base_event(js_devnode, *js_syspath);
      js_event["ID_INPUT_JOYSTICK"] = "1";
      js_event[".INPUT_CLASS"] = "joystick";
      assignment.udev_events.push_back(js_event);
      assignment.udev_hw_db_entries.push_back({gen_udev_hw_db_filename(js_devnode), hwdb_rows});
    }
  } catch (const std::filesystem::filesystem_error &) {
  }

  return assignment;
}

bool is_joinable_gamepad(libevdev *dev) {
  if (!dev) {
    return false;
  }
  if (!libevdev_has_event_type(dev, EV_KEY) || !libevdev_has_event_type(dev, EV_ABS)) {
    return false;
  }
  if (!libevdev_has_event_code(dev, EV_KEY, BTN_START) || !libevdev_has_event_code(dev, EV_KEY, BTN_SELECT)) {
    return false;
  }
  return libevdev_has_event_code(dev, EV_KEY, BTN_GAMEPAD) || libevdev_has_event_code(dev, EV_KEY, BTN_SOUTH);
}

std::optional<WatchedGamepad> try_open_gamepad(const std::string &devnode) {
  auto fd = open(devnode.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }

  libevdev *raw_dev = nullptr;
  if (libevdev_new_from_fd(fd, &raw_dev) < 0) {
    close(fd);
    return std::nullopt;
  }

  auto dev = make_libevdev_ptr(raw_dev);
  if (!is_joinable_gamepad(dev.get())) {
    close(fd);
    return std::nullopt;
  }

  return WatchedGamepad{
      .devnode = devnode,
      .fd = fd,
      .dev = dev,
  };
}

std::optional<std::size_t> find_join_target_session(const immer::box<state::AppState> &app_state) {
  auto running_sessions = app_state->running_sessions->load();
  auto party_sessions = app_state->party_mode_sessions->load();

  std::set<std::size_t> secondary_sessions;
  std::set<std::size_t> primary_sessions_with_party;
  for (const auto &party_session : *party_sessions) {
    secondary_sessions.insert(party_session.secondary_session_id);
    primary_sessions_with_party.insert(party_session.primary_session_id);
  }

  std::vector<std::size_t> candidates;
  for (const auto &session : *running_sessions) {
    if (secondary_sessions.count(session.session_id) == 0 &&
        primary_sessions_with_party.count(session.session_id) == 0) {
      candidates.push_back(session.session_id);
    }
  }

  if (candidates.size() == 1) {
    return candidates.front();
  }
  return std::nullopt;
}

void maybe_release_grab(WatchedGamepad &watcher) {
  if (watcher.dev && watcher.grabbed && (!watcher.select_down || !watcher.start_down)) {
    libevdev_grab(watcher.dev.get(), LIBEVDEV_UNGRAB);
    watcher.grabbed = false;
    watcher.both_pressed_since = std::nullopt;
  }
}

void scan_for_new_gamepads(std::map<std::string, WatchedGamepad> &watchers,
                           const immer::box<state::AppState> &app_state) {
  std::set<std::string> assigned_devnodes;
  for (const auto &assignment : *app_state->physical_input_devices->load()) {
    assigned_devnodes.insert(assignment.devnode);
  }

  for (auto it = watchers.begin(); it != watchers.end();) {
    if (assigned_devnodes.count(it->first) > 0 || !std::filesystem::exists(it->first)) {
      close_watcher(it->second);
      it = watchers.erase(it);
    } else {
      ++it;
    }
  }

  if (!std::filesystem::exists("/dev/input")) {
    return;
  }

  try {
    for (const auto &entry : std::filesystem::directory_iterator("/dev/input")) {
      if (!entry.is_character_file()) {
        continue;
      }
      auto path = entry.path().string();
      if (entry.path().filename().string().rfind("event", 0) != 0) {
        continue;
      }
      if (watchers.count(path) > 0 || assigned_devnodes.count(path) > 0) {
        continue;
      }
      if (auto watcher = try_open_gamepad(path)) {
        logs::log(logs::info, "[PARTY] Watching local gamepad {}", path);
        watchers.emplace(path, std::move(*watcher));
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    logs::log(logs::warning, "[PARTY] Failed to scan /dev/input for local gamepads: {}", e.what());
  }
}

} // namespace

void start_party_mode_join_listener(const immer::box<state::AppState> &app_state) {
  std::thread([app_state]() {
    std::map<std::string, WatchedGamepad> watchers;

    while (true) {
      scan_for_new_gamepads(watchers, app_state);

      for (auto it = watchers.begin(); it != watchers.end();) {
        auto &watcher = it->second;
        input_event ev{};
        int rc = libevdev_next_event(watcher.dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev);
        while (rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == LIBEVDEV_READ_STATUS_SYNC) {
          if (ev.type == EV_KEY) {
            if (ev.code == BTN_SELECT) {
              watcher.select_down = ev.value != 0;
            } else if (ev.code == BTN_START) {
              watcher.start_down = ev.value != 0;
            }

            if ((ev.code == BTN_SELECT || ev.code == BTN_START) && (watcher.select_down || watcher.start_down) &&
                !watcher.grabbed) {
              if (libevdev_grab(watcher.dev.get(), LIBEVDEV_GRAB) == 0) {
                watcher.grabbed = true;
              }
            }

            if (watcher.select_down && watcher.start_down) {
              if (!watcher.both_pressed_since) {
                watcher.both_pressed_since = std::chrono::steady_clock::now();
              } else if (!watcher.chord_triggered &&
                         std::chrono::steady_clock::now() - *watcher.both_pressed_since >= JOIN_HOLD_DURATION) {
                watcher.chord_triggered = true;
                auto target_session = find_join_target_session(app_state);
                if (!target_session) {
                  logs::log(logs::warning,
                            "[PARTY] Unable to determine a single session to join for {}",
                            watcher.devnode);
                } else if (auto secondary_session =
                               create_party_mode_secondary_session(app_state, *target_session, false)) {
                  auto assignment_info = inspect_gamepad_assignment(watcher.devnode,
                                                                    watcher.dev.get(),
                                                                    std::to_string(secondary_session->session_id));
                  if (assignment_info) {
                    app_state->physical_input_devices->update([assignment_info](const auto &assignments) {
                      return state::remove_physical_input_device_assignment_by_devnode(assignments,
                                                                                       assignment_info->devnode)
                          .push_back(*assignment_info);
                    });
                    auto plug_device_event = events::PlugDeviceEvent{
                        .session_id = assignment_info->routed_session_id,
                        .udev_events = assignment_info->udev_events,
                        .udev_hw_db_entries = assignment_info->udev_hw_db_entries};
                    app_state->event_bus->fire_event(immer::box<events::PlugDeviceEvent>(plug_device_event));
                    logs::log(logs::info,
                              "[PARTY] Local gamepad {} joined session {} via seat {}",
                              watcher.devnode,
                              *target_session,
                              secondary_session->session_id);
                    close_watcher(watcher);
                    it = watchers.erase(it);
                    goto next_watcher;
                  } else {
                    logs::log(logs::warning,
                              "[PARTY] Failed to inspect local gamepad {}, tearing down secondary seat {}",
                              watcher.devnode,
                              secondary_session->session_id);
                    app_state->event_bus->fire_event(immer::box<events::StopStreamEvent>{
                        events::StopStreamEvent{.session_id = secondary_session->session_id}});
                  }
                }
              }
            } else {
              watcher.chord_triggered = false;
              maybe_release_grab(watcher);
            }
          }

          rc = libevdev_next_event(watcher.dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev);
        }

        maybe_release_grab(watcher);
        ++it;
      next_watcher:
        continue;
      }

      auto assignments = app_state->physical_input_devices->load();
      for (const auto &assignment : *assignments) {
        if (assignment.stop_session_on_disconnect && !std::filesystem::exists(assignment.devnode)) {
          logs::log(logs::info,
                    "[PARTY] Local gamepad {} disconnected, stopping seat {}",
                    assignment.devnode,
                    assignment.owner_session_id);
          app_state->physical_input_devices->update([devnode = assignment.devnode](const auto &current_assignments) {
            return state::remove_physical_input_device_assignment_by_devnode(current_assignments, devnode);
          });
          app_state->event_bus->fire_event(immer::box<events::StopStreamEvent>{
              events::StopStreamEvent{.session_id = std::stoull(assignment.owner_session_id)}});
        }
      }

      std::this_thread::sleep_for(50ms);
    }
  }).detach();
}

} // namespace wolf::core::sessions
