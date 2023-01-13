#include <boost/asio/io_service.hpp>
#include <boost/thread.hpp>
#include <future>
#include <helpers/logger.hpp>
#include <immer/atom.hpp>
#include <moonlight/data-structures.hpp>
#include <process/process.hpp>

namespace process {

using namespace moonlight;

void run_process(immer::box<START_PROCESS_EV> process_ev) {
  logs::log(logs::debug, "[PROCESS] Starting process: {}", process_ev->run_cmd);

  std::future<std::string> std_out, err_out;
  boost::asio::io_service ios;
  bp::child child_proc;
  bp::group group_proc;

  try {
    child_proc = bp::child(process_ev->run_cmd,
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
  auto terminate_handler = process_ev->event_bus->register_handler<immer::box<control::TerminateEvent>>(
      [&client_connected, &group_proc, sess_id = process_ev->session_id](
          immer::box<control::TerminateEvent> terminate_ev) {
        if (terminate_ev->session_id == sess_id) {
          client_connected.store(false);
          group_proc.terminate(); // Manually terminate the process
        }
      });

  ios.run(); // This will stop here until the process is over

  if (*client_connected.load()) {
    // TODO: the process terminated in error before the user closed the connection
    // We should signal this to the control event so that it can close the connection downstream
    // fire_event( ... )
  }
  child_proc.wait(); // to avoid a zombie process & get the exit code

  auto ex_code = child_proc.exit_code();
  logs::log(logs::debug, "[PROCESS] Terminated with status code: {}\nstd_out: {}", ex_code, std_out.get());
  if (!err_out.get().empty()) {
    logs::log(logs::warning, "[PROCESS] Terminated with status code: {}, std_err: {}", ex_code, err_out.get());
  }

  terminate_handler.unregister();
}

std::thread spawn_process(immer::box<START_PROCESS_EV> process_ev) {
  return std::thread(run_process, process_ev);
}
} // namespace process