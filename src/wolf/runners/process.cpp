#include <boost/asio/io_service.hpp>
#include <future>
#include <helpers/logger.hpp>
#include <immer/atom.hpp>
#include <moonlight/data-structures.hpp>
#include <runners/process.hpp>

namespace process {

using namespace moonlight::control;

void RunProcess::run(std::size_t session_id,
                     const immer::array<std::string> &virtual_inputs,
                     const immer::map<std::string_view, std::string_view> &env_variables) {
  logs::log(logs::debug, "[PROCESS] Starting process: {}", this->run_cmd);

  std::future<std::string> std_out, err_out;
  boost::asio::io_service ios;
  bp::child child_proc;
  bp::group group_proc;

  try {
    auto env = boost::this_process::environment();
    for (const auto &env_var : env_variables) {
      env[env_var.first.data()] = env_var.second.data();
    };

    child_proc = bp::child(this->run_cmd,
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

  auto terminate_handler = this->ev_bus->register_handler<immer::box<moonlight::StopStreamEvent>>(
      [&group_proc, session_id](const immer::box<moonlight::StopStreamEvent> &terminate_ev) {
        if (terminate_ev->session_id == session_id) {
          group_proc.terminate(); // Manually terminate the process
        }
      });

  auto pause_handler = this->ev_bus->register_handler<immer::box<moonlight::PauseStreamEvent>>(
      [&group_proc, session_id](const immer::box<moonlight::PauseStreamEvent> &terminate_ev) {
        if (terminate_ev->session_id == session_id) {
          group_proc.terminate(); // Manually terminate the process
        }
      });

  ios.run();         // This will stop here until the process is over
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