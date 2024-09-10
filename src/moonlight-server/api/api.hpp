#pragma once

#include <api/http_server.hpp>
#include <events/events.hpp>
#include <state/data-structures.hpp>

namespace wolf::api {

void start_server(immer::box<state::AppState> app_state);

struct PairRequest {
  std::string pair_secret;
  rfl::Description<"The PIN created by the remote Moonlight client", std::string> pin;
};

struct PairResponse {
  bool success = true;
};

struct PairResponseError {
  bool success = false;
  std::string error;
};

struct PendingPairRequestsResponse {
  bool success = true;
  std::vector<PairRequest> requests;
};

struct UnixSocket {
  boost::asio::local::stream_protocol::socket socket;
  bool is_alive = true;
};

class UnixSocketServer {
public:
  UnixSocketServer(boost::asio::io_context &io_context,
                   const std::string &socket_path,
                   immer::box<state::AppState> app_state);

  void broadcast_event(const std::string &event_json);

  void cleanup_sockets();

private:
  void endpoint_Events(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_PendingPairRequest(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_Pair(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);

  void send_http(std::shared_ptr<UnixSocket> socket, int status_code, std::string_view body);
  void handle_request(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void start_connection(std::shared_ptr<UnixSocket> socket);
  void start_accept();
  void close(UnixSocket &socket);

  boost::asio::io_context &io_context_;
  boost::asio::local::stream_protocol::acceptor acceptor_;
  std::vector<std::shared_ptr<UnixSocket>> sockets_ = {};
  immer::box<state::AppState> app_state;
  HTTPServer<std::shared_ptr<UnixSocket>> http = {};
};

} // namespace wolf::api