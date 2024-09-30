#pragma once

#include <chrono>
#include <events/events.hpp>

namespace wolf::core::coop {

using namespace wolf::core;
using namespace std::chrono_literals;

/**
 * A child session will just forward all the events to the parent session.
 * It's used to implement co-op sessions where a different client connects to another active session
 */
class RunChildSession : public events::Runner {
public:
  RunChildSession(std::size_t parent_session_id, std::shared_ptr<events::EventBusType> ev_bus)
      : ev_bus(std::move(ev_bus)), parent_session_id(parent_session_id) {};

  void run(std::size_t session_id,
           std::string_view app_state_folder,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override {
    /* Keep a history of plugged devices so that we can clean up when over */
    std::vector<immer::box<events::PlugDeviceEvent>> plugged_devices = {};
    /* true when this session should quit */
    std::shared_ptr<std::atomic_bool> is_over = std::make_shared<std::atomic<bool>>(false);

    auto stop_handler = ev_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [session_id, is_over](const immer::box<events::StopStreamEvent> &terminate_ev) {
          if (terminate_ev->session_id == session_id) {
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

  rfl::TaggedUnion<"type", wolf::config::AppCMD, wolf::config::AppDocker, wolf::config::AppChildSession>
  serialize() override {
    return wolf::config::AppChildSession{.parent_session_id = parent_session_id};
  }

private:
  std::shared_ptr<events::EventBusType> ev_bus;
  std::size_t parent_session_id;
};

} // namespace wolf::core::coop
