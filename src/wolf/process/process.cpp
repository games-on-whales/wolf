#include <boost/asio/io_service.hpp>
#include <boost/thread.hpp>
#include <future>
#include <helpers/logger.hpp>
#include <immer/atom.hpp>
#include <moonlight/data-structures.hpp>
#include <process/process.hpp>

namespace process {

using namespace moonlight::control;

void run_process(const immer::box<state::LaunchAPPEvent> &process_ev) {
  logs::log(logs::debug, "[PROCESS] Starting process: {}", process_ev->app_launch_cmd);

  std::future<std::string> std_out, err_out;
  boost::asio::io_service ios;
  bp::child child_proc;
  bp::group group_proc;

  try {
    auto env = boost::this_process::environment();
    if (process_ev->wayland_socket) {
      env["WAYLAND_DISPLAY"] = process_ev->wayland_socket.value();
    }
    if (process_ev->xorg_socket) {
      env["DISPLAY"] = process_ev->xorg_socket.value();
    }
    child_proc = bp::child(process_ev->app_launch_cmd,
                           env,
                           bp::std_in.close(),
                           bp::std_out > std_out,
                           bp::std_err > err_out,
                           ios,
                           group_proc);
  } catch (const std::system_error &e) {
    logs::log(logs::error, "Unable to start process, error: {} - {}", e.code().value(), e.what());
    return;
  }

  auto client_connected = immer::atom<bool>(true);
  auto terminate_handler = process_ev->event_bus->register_handler<immer::box<TerminateEvent>>(
      [&client_connected, &group_proc, sess_id = process_ev->session_id](
          const immer::box<TerminateEvent> &terminate_ev) {
        if (terminate_ev->session_id == sess_id) {
          client_connected.store(false);
          group_proc.terminate(); // Manually terminate the process
        }
      });

  ios.run(); // This will stop here until the process is over

  if (*client_connected.load()) {
    logs::log(logs::warning, "[PROCESS] Process terminated before the user closed the connection.");
    process_ev->event_bus->fire_event(
        immer::box<AppStoppedEvent>(AppStoppedEvent{.session_id = process_ev->session_id}));
  }
  child_proc.wait(); // to avoid a zombie process & get the exit code

  auto ex_code = child_proc.exit_code();
  logs::log(logs::debug, "[PROCESS] Terminated with status code: {}\nstd_out: {}", ex_code, std_out.get());
  auto errors = err_out.get();
  if (!errors.empty()) {
    logs::log(logs::warning, "[PROCESS] Terminated with status code: {}, std_err: {}", ex_code, errors);
  }

  terminate_handler.unregister();
}

} // namespace process