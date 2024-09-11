#include <api/api.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <events/reflectors.hpp>
#include <memory>
#include <rfl/json.hpp>

namespace wolf::api {

using namespace wolf::core;

void start_server(immer::box<state::AppState> app_state) {
  auto socket_path = "/tmp/wolf.sock";
  logs::log(logs::info, "Starting API server on {}", socket_path);

  ::unlink(socket_path);
  boost::asio::io_context io_context;
  UnixSocketServer server(io_context, socket_path, app_state);
  auto server_ptr = std::make_shared<UnixSocketServer>(server);

  auto global_ev_handler = app_state->event_bus->register_global_handler([server_ptr](events::EventsVariant ev) {
    std::visit([server_ptr](auto &&arg) { server_ptr->broadcast_event(arg->event_type, rfl::json::write(*arg)); }, ev);
  });

  io_context.run();
}

} // namespace wolf::api