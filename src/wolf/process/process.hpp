#pragma once
#include <boost/process.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <string>
#include <thread>

namespace process {

namespace bp = boost::process;

struct START_PROCESS_EV {
  std::size_t session_id;
  std::shared_ptr<dp::event_bus> event_bus;

  std::string run_cmd;
};

std::thread spawn_process(immer::box<START_PROCESS_EV> process_ev);

void run_process(immer::box<START_PROCESS_EV> process_ev);

} // namespace process