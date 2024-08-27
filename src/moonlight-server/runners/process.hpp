#pragma once
#include <boost/process.hpp>
#include <core/input.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <memory>
#include <state/data-structures.hpp>
#include <string>
#include <thread>
#include <toml.hpp>

namespace process {

using namespace wolf::core;

class RunProcess : public events::Runner {
public:
  explicit RunProcess(std::shared_ptr<dp::event_bus<events::EventTypes>> ev_bus, std::string run_cmd)
      : run_cmd(std::move(run_cmd)), ev_bus(std::move(ev_bus)) {}

  void run(std::size_t session_id,
           std::string_view app_state_folder,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override;

  toml::value serialise() override {
    return toml::table{{"type", "process"}, {"run_cmd", this->run_cmd}};
  }

protected:
  std::string run_cmd;
  std::shared_ptr<dp::event_bus<events::EventTypes>> ev_bus;
};
namespace bp = boost::process;

} // namespace process