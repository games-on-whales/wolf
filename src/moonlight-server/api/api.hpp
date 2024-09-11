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

  UnixSocketServer(const UnixSocketServer &) = default;

  void broadcast_event(const std::string &event_type, const std::string &event_json);

private:
  void endpoint_Events(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_PendingPairRequest(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_Pair(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);

  void sse_broadcast(const std::string &payload);
  void sse_keepalive(const boost::system::error_code &e);

  void send_http(std::shared_ptr<UnixSocket> socket, int status_code, std::string_view body);
  void send_http(std::shared_ptr<UnixSocket> socket,
                 int status_code,
                 const std::vector<std::string_view> &http_headers,
                 std::string_view body);

  void handle_request(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void start_connection(std::shared_ptr<UnixSocket> socket);
  void start_accept();

  void cleanup_sockets();
  void close(UnixSocket &socket);

  struct UnixSocketState {
    boost::asio::io_context &io_context;
    immer::box<state::AppState> app_state;
    boost::asio::local::stream_protocol::acceptor acceptor;
    std::vector<std::shared_ptr<UnixSocket>> sockets;
    HTTPServer<std::shared_ptr<UnixSocket>> http;
    boost::asio::steady_timer sse_keepalive_timer;
  };

  std::shared_ptr<UnixSocketState> state_;
};

} // namespace wolf::api