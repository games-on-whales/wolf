#pragma once

#include <api/http_server.hpp>
#include <events/events.hpp>
#include <events/reflectors.hpp>
#include <state/data-structures.hpp>

namespace wolf::api {

using namespace wolf::core;

void start_server(immer::box<state::AppState> app_state);

struct PairRequest {
  std::string pair_secret;
  rfl::Description<"The PIN created by the remote Moonlight client", std::string> pin;
};

struct GenericSuccessResponse {
  bool success = true;
};

struct GenericErrorResponse {
  bool success = false;
  std::string error;
};

struct PendingPairRequestsResponse {
  bool success = true;
  std::vector<PairRequest> requests;
};

struct PairedClient {
  std::size_t client_id;
  std::string app_state_folder;
};

struct PairedClientsResponse {
  bool success = true;
  std::vector<PairedClient> clients;
};

struct AppListResponse {
  bool success = true;
  std::vector<rfl::Reflector<wolf::core::events::App>::ReflType> apps;
};

struct AppDeleteRequest {
  std::string id;
};

struct StreamSessionListResponse {
  bool success = true;
  std::vector<rfl::Reflector<wolf::core::events::StreamSession>::ReflType> sessions;
};

struct StreamSessionPauseRequest {
  std::string session_id;
};

struct StreamSessionStopRequest {
  std::string session_id;
};

struct RunnerStartRequest {
  bool stop_stream_when_over;
  rfl::TaggedUnion<"type", wolf::config::AppCMD, wolf::config::AppDocker, wolf::config::AppChildSession> runner;
  std::string session_id;
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
  void endpoint_PairedClients(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);

  void endpoint_Apps(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_AddApp(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_RemoveApp(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);

  void endpoint_StreamSessions(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_StreamSessionAdd(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_StreamSessionPause(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);
  void endpoint_StreamSessionStop(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);

  void endpoint_RunnerStart(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket);

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