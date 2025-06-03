#pragma once
#include <boost/version.hpp>
#if BOOST_VERSION < 108800
#include <boost/process.hpp>
namespace bp = boost::process;
#else
#include <boost/process/v1/async.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/env.hpp>
#include <boost/process/v1/group.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/start_dir.hpp>
namespace bp = boost::process::v1;
#endif
#include <core/input.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <memory>
#include <state/data-structures.hpp>
#include <string>
#include <thread>

namespace process {

using namespace wolf::core;

class RunProcess : public events::Runner {
public:
  explicit RunProcess(std::shared_ptr<events::EventBusType> ev_bus, std::string run_cmd)
      : run_cmd(std::move(run_cmd)), ev_bus(std::move(ev_bus)) {}

  void run(std::size_t session_id,
           std::string_view app_state_folder,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override;

  rfl::TaggedUnion<"type", wolf::config::AppCMD, wolf::config::AppDocker, wolf::config::AppChildSession>
  serialize() override {
    return wolf::config::AppCMD{.run_cmd = run_cmd};
  }

protected:
  std::string run_cmd;
  std::shared_ptr<events::EventBusType> ev_bus;
};

} // namespace process