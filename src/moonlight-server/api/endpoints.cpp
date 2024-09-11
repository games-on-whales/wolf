#include <api/api.hpp>

namespace wolf::api {

void UnixSocketServer::endpoint_Events(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  // curl -N --unix-socket /tmp/wolf.sock http://localhost/api/v1/events
  state_->sockets.push_back(socket);
  send_http(socket,
            200,
            {{"Content-Type: text/event-stream"}, {"Connection: keep-alive"}, {"Cache-Control: no-cache"}},
            ""); // Inform clients this is going to be SSE
}

void UnixSocketServer::endpoint_PendingPairRequest(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  // curl -v --http1.0 --unix-socket /tmp/wolf.sock http://localhost/api/v1/pending-pair-requests
  auto res = PendingPairRequestsResponse{.success = true};
  auto requests = std::vector<PairRequest>();
  for (auto [secret, pair_request] : *(state_->app_state)->pairing_atom->load()) {
    requests.push_back({.pair_secret = secret, .pin = pair_request->client_ip});
  }
  send_http(socket, 200, rfl::json::write(res));
}

void UnixSocketServer::endpoint_Pair(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  // curl -v --http1.0 --unix-socket /tmp/wolf.sock -d '{"pair_secret": "xxxx", "pin": "1234"}'
  // http://localhost/api/v1/pair-client
  if (auto event = rfl::json::read<PairRequest>(req.body)) {
    if (auto pair_request = state_->app_state->pairing_atom->load()->find(event.value().pair_secret)) {
      pair_request->get().user_pin->set_value(event.value().pin.value()); // Resolve the promise
      auto res = PairResponse{.success = true};
      send_http(socket, 200, rfl::json::write(res));
    } else {
      logs::log(logs::warning, "[API] Invalid pair secret: {}", event.value().pair_secret);
      auto res = PairResponseError{.error = "Invalid pair secret"};
      send_http(socket, 500, rfl::json::write(res));
    }
  } else {
    logs::log(logs::warning, "[API] Invalid event: {}", req.body);
    auto res = PairResponseError{.error = "Invalid event"};
    send_http(socket, 500, rfl::json::write(res));
  }
}

} // namespace wolf::api