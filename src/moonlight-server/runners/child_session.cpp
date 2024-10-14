#include <chrono>
#include <core/virtual-display.hpp>
#include <runners/child_session.hpp>
#include <state/sessions.hpp>

namespace wolf::core::coop {

using namespace wolf::core;
using namespace std::chrono_literals;

void RunChildSession::run(std::size_t session_id,
                          std::string_view app_state_folder,
                          std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
                          const immer::array<std::string> &virtual_inputs,
                          const immer::array<std::pair<std::string, std::string>> &paths,
                          const immer::map<std::string, std::string> &env_variables,
                          std::string_view render_node) {

  auto child_session = state::get_session_by_id(running_sessions->load(), session_id);
  auto parent_session = state::get_session_by_id(running_sessions->load(), parent_session_id);

  if (!child_session.has_value() || !parent_session.has_value()) {
    logs::log(logs::error, "Unable to run child session, could not find parent or child session");
    return;
  }

  /* inherit the wayland connection, needed in order to advertise some devices (ex: PS5 trackpad) */
  if (auto wl = *parent_session->wayland_display->load()) {
    child_session->wayland_display = parent_session->wayland_display;

    /* Add mouse and keyboards to our wayland display */
    if (child_session->mouse->has_value()) {
      if (auto mouse = std::get_if<input::Mouse>(&child_session->mouse->value())) {
        for (auto path : mouse->get_nodes()) {
          add_input_device(*wl, path);
        }
      }
    }

    if (child_session->keyboard->has_value()) {
      if (auto kb = std::get_if<input::Keyboard>(&child_session->keyboard->value())) {
        for (auto path : kb->get_nodes()) {
          add_input_device(*wl, path);
        }
      }
    }
  }

  /* Keep a history of plugged devices so that we can clean up when over */
  std::vector<immer::box<events::PlugDeviceEvent>> plugged_devices = {};
  /* true when this session should quit */
  std::shared_ptr<std::atomic_bool> is_over = std::make_shared<std::atomic<bool>>(false);

  auto stop_handler = ev_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [session_id, parent_session_id = parent_session_id, is_over](
          const immer::box<events::StopStreamEvent> &terminate_ev) {
        if (terminate_ev->session_id == session_id || terminate_ev->session_id == parent_session_id) {
          *is_over = true;
        }
      });

  auto unplug_handler = ev_bus->register_handler<immer::box<events::UnplugDeviceEvent>>(
      [session_id, parent_id = parent_session_id, ev_bus = ev_bus](const immer::box<events::UnplugDeviceEvent> &ev) {
        if (ev->session_id == session_id) {
          events::UnplugDeviceEvent unplug_ev = *ev;
          unplug_ev.session_id = parent_id;
          ev_bus->fire_event(immer::box<events::UnplugDeviceEvent>(unplug_ev));
        }
      });

  while (!*is_over) {
    while (auto device_ev = plugged_devices_queue->pop(500ms)) {
      if (device_ev->get().session_id == session_id) {
        events::PlugDeviceEvent plug_ev = device_ev->get();
        plug_ev.session_id = parent_session_id;
        ev_bus->fire_event(immer::box<events::PlugDeviceEvent>(plug_ev));
        plugged_devices.push_back(plug_ev);
      }
    }
  }

  // This child session is over, unplug all devices that we've plugged
  for (const auto &device_ev : plugged_devices) {
    events::UnplugDeviceEvent unplug_ev;
    unplug_ev.session_id = parent_session_id;
    unplug_ev.udev_hw_db_entries = device_ev->udev_hw_db_entries;
    unplug_ev.udev_events = device_ev->udev_events;
    ev_bus->fire_event(immer::box<events::UnplugDeviceEvent>(unplug_ev));
  }
}

} // namespace wolf::core::coop
