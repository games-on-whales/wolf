#pragma once
#include <boost/process.hpp>
#include <eventbus/event_bus.hpp>
#include <immer/box.hpp>
#include <memory>
#include <string>
#include <thread>

namespace process {

/**
 * This event will trigger the start of the application command
 */
struct LaunchAPPEvent {
  std::size_t session_id;
  std::shared_ptr<dp::event_bus> event_bus;

  /**
   * The full command to be launched
   */
  std::string app_launch_cmd;
};

struct AppStoppedEvent{
  std::size_t session_id;
};

namespace bp = boost::process;

void run_process(immer::box<process::LaunchAPPEvent> process_ev);

} // namespace process