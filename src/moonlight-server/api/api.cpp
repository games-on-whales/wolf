#include <api/api.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <events/reflectors.hpp>
#include <filesystem>
#include <helpers/utils.hpp>
#include <memory>
#include <rfl/json.hpp>

namespace wolf::api {

using namespace wolf::core;

void start_server(std::string_view runtime_dir, immer::box<state::AppState> app_state) {
  auto default_socket_path = std::filesystem::path(runtime_dir) / "wolf.sock";
  auto socket_path = utils::get_env("WOLF_SOCKET_PATH", default_socket_path.c_str());
  logs::log(logs::info, "Starting Wolf API server on {}", socket_path);

  ::unlink(socket_path);
  boost::asio::io_context io_context;
  UnixSocketServer server(io_context, socket_path, app_state);
  auto server_ptr = std::make_shared<UnixSocketServer>(server);

  auto global_ev_handler = app_state->event_bus->register_global_handler([server_ptr](events::EventsVariant ev) {
    std::visit(
        [server_ptr](auto &&arg) {
          const auto event_type = rfl::type_name_t<std::decay_t<decltype(*arg)>>().str();
          server_ptr->broadcast_event(event_type, rfl::json::write(*arg));
        },
        ev);
  });

  // Release any fake-uinput devices still recorded for a session or lobby that tears down, so an
  // unplug that never arrives (app killed) doesn't leave entries behind.
  auto stop_stream_handler = app_state->event_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [server_ptr](const immer::box<events::StopStreamEvent> &ev) {
        server_ptr->purge_fake_uinput_devices(std::to_string(ev->session_id));
      });

  auto stop_lobby_handler = app_state->event_bus->register_handler<immer::box<events::StopLobbyEvent>>(
      [server_ptr](const immer::box<events::StopLobbyEvent> &ev) {
        server_ptr->purge_fake_uinput_devices(ev->lobby_id);
      });

  io_context.run();
}

} // namespace wolf::api