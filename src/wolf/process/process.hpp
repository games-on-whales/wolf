#pragma once
#include <boost/process.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <streaming/data-structures.hpp>
#include <memory>
#include <string>
#include <thread>

namespace process {

struct AppStoppedEvent {
  std::size_t session_id;
};

namespace bp = boost::process;

void run_process(immer::box<state::LaunchAPPEvent> process_ev);

} // namespace process