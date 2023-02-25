#pragma once
#include <boost/process.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <memory>
#include <streaming/data-structures.hpp>
#include <string>
#include <thread>

namespace process {

namespace bp = boost::process;

void run_process(const immer::box<state::LaunchAPPEvent> &process_ev);

} // namespace process