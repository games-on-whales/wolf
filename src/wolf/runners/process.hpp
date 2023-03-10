#pragma once
#include <boost/process.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <memory>
#include <state/data-structures.hpp>
#include <streaming/data-structures.hpp>
#include <string>
#include <thread>
#include <toml.hpp>

namespace process {

class RunProcess : public state::Runner {
public:
  explicit RunProcess(std::shared_ptr<dp::event_bus> ev_bus, std::string run_cmd)
      : run_cmd(std::move(run_cmd)), ev_bus(std::move(ev_bus)) {}

  void run(std::size_t session_id,
           const immer::array<std::string> &virtual_inputs,
           const immer::map<std::string, std::string> &env_variables) override;

  toml::value serialise() override {
    return {{"type", "process"}, {"run_cmd", this->run_cmd}};
  }

protected:
  std::string run_cmd;
  std::shared_ptr<dp::event_bus> ev_bus;
};
namespace bp = boost::process;

} // namespace process