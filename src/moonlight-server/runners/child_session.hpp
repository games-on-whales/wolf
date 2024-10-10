#pragma once

#include <events/events.hpp>
#include <state/data-structures.hpp>

namespace wolf::core::coop {

/**
 * A child session will just forward all the events to the parent session.
 * It's used to implement co-op sessions where a different client connects to another active session
 */
class RunChildSession : public events::Runner {
public:
  RunChildSession(std::size_t parent_session_id,
                  std::shared_ptr<events::EventBusType> ev_bus,
                  state::SessionsAtoms running_sessions)
      : ev_bus(std::move(ev_bus)), parent_session_id(parent_session_id),
        running_sessions(std::move(running_sessions)) {};

  void run(std::size_t session_id,
           std::string_view app_state_folder,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override;

  rfl::TaggedUnion<"type", wolf::config::AppCMD, wolf::config::AppDocker, wolf::config::AppChildSession>
  serialize() override {
    return wolf::config::AppChildSession{.parent_session_id = std::to_string(parent_session_id)};
  }

private:
  std::shared_ptr<events::EventBusType> ev_bus;
  std::size_t parent_session_id;
  state::SessionsAtoms running_sessions;
};

} // namespace wolf::core::coop