#include <api/api.hpp>

namespace wolf::api {

using namespace wolf::core;

constexpr auto SSE_KEEPALIVE_INTERVAL = std::chrono::seconds(15);

std::string to_str(boost::asio::streambuf &streambuf, std::size_t start, std::size_t end) {
  return {buffers_begin(streambuf.data()) + start, buffers_begin(streambuf.data()) + start + end};
}

std::string to_str(boost::asio::streambuf &streambuf, std::size_t end) {
  return to_str(streambuf, 0, end);
}

UnixSocketServer::UnixSocketServer(boost::asio::io_context &io_context,
                                   const std::string &socket_path,
                                   immer::box<state::AppState> app_state) {

  state_ = std::make_shared<UnixSocketState>(io_context, app_state, socket_path);

  state_->http.add(HTTPMethod::GET,
                   "/api/v1/events",
                   {.summary = "Subscribe to events",
                    .description = "This endpoint allows clients to subscribe to events using SSE",
                    // TODO: json_schema = rfl::json::to_schema<EventsVariant>()
                    .handler = [this](auto req, auto socket) { endpoint_Events(req, socket); }});

  /**
   * Pairing API
   */

  state_->http.add(
      HTTPMethod::GET,
      "/api/v1/pair/pending",
      {
          .summary = "Get pending pair requests",
          .description = "This endpoint returns a list of Moonlight clients that are currently waiting to be paired.",
          .response_description = {{200, {.json_schema = rfl::json::to_schema<PendingPairRequestsResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_PendingPairRequest(req, socket); },
      });

  state_->http.add(HTTPMethod::POST,
                   "/api/v1/pair/client",
                   {
                       .summary = "Pair a client",
                       .request_description = APIDescription{.json_schema = rfl::json::to_schema<PairRequest>()},
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                                {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_Pair(req, socket); },
                   });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/unpair/client",
      {
          .summary = "Unpair a client",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<UnpairClientRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_UnpairClient(req, socket); },
      });

  state_->http.add(HTTPMethod::GET,
                   "/api/v1/clients",
                   {
                       .summary = "Get paired clients",
                       .description = "This endpoint returns a list of all paired clients.",
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<PairedClientsResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_PairedClients(req, socket); },
                   });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/clients/settings",
      {
          .summary = "Update client settings",
          .description = "Update a client's settings including app state folder and client-specific settings",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<UpdateClientSettingsRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {400, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}},
                                   {404, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_UpdateClientSettings(req, socket); },
      });

  /**
   * Apps API
   */

  state_->http.add(
      HTTPMethod::GET,
      "/api/v1/apps",
      {
          .summary = "Get all Moonlight apps",
          .description = "This endpoint returns a list of all apps that will be shown in the Moonlight client.",
          .response_description = {{200, {.json_schema = rfl::json::to_schema<AppListResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_Apps(req, socket); },
      });

  state_->http.add(HTTPMethod::POST,
                   "/api/v1/apps/add",
                   {
                       .summary = "Add a Moonlight app",
                       .request_description =
                           APIDescription{.json_schema = rfl::json::to_schema<rfl::Reflector<events::App>::ReflType>()},
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                                {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_AddApp(req, socket); },
                   });

  state_->http.add(HTTPMethod::POST,
                   "/api/v1/apps/delete",
                   {.summary = "Remove a Moonlight app",
                    .request_description = APIDescription{.json_schema = rfl::json::to_schema<AppDeleteRequest>()},
                    .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                             {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
                    .handler = [this](auto req, auto socket) { endpoint_RemoveApp(req, socket); }});

  /**
   * Profiles API
   */

  state_->http.add(HTTPMethod::GET,
                   "/api/v1/profiles",
                   {
                       .summary = "Get all profiles",
                       .description = "This endpoint returns a list of all profiles.",
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<ProfileListResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_Profiles(req, socket); },
                   });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/profiles/add",
      {
          .summary = "Create a new profile",
          .request_description =
              APIDescription{.json_schema = rfl::json::to_schema<rfl::Reflector<events::Profile>::ReflType>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_AddProfile(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/profiles/remove",
      {
          .summary = "Remove a profile",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<ProfileRemoveRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_RemoveProfile(req, socket); },
      });

  /**
   * Stream session API
   */

  state_->http.add(
      HTTPMethod::GET,
      "/api/v1/sessions",
      {
          .summary = "Get all stream sessions",
          .description = "This endpoint returns a list of all active stream sessions.",
          .response_description = {{200, {.json_schema = rfl::json::to_schema<StreamSessionListResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_StreamSessions(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/sessions/add",
      {
          .summary = "Create a new stream session",
          .request_description =
              APIDescription{.json_schema = rfl::json::to_schema<rfl::Reflector<events::StreamSession>::ReflType>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<StreamSessionCreated>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_StreamSessionAdd(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/sessions/start",
      {
          .summary = "Start a stream session",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<StreamSessionStartRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_StreamSessionStart(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/sessions/pause",
      {
          .summary = "Pause a stream session",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<StreamSessionPauseRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_StreamSessionPause(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/sessions/stop",
      {
          .summary = "Stop a stream session",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<StreamSessionStopRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_StreamSessionStop(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/sessions/input",
      {
          .summary = "Handle input for a stream session",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<StreamSessionHandleInputRequest>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_StreamSessionHandleInput(req, socket); },
      });

  state_->http.add(HTTPMethod::POST,
                   "/api/v1/runners/start",
                   {
                       .summary = "Start a runner in a given session",
                       .request_description = APIDescription{.json_schema = rfl::json::to_schema<RunnerStartRequest>()},
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                                {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_RunnerStart(req, socket); },
                   });

  /**
   * Lobbies API
   */

  state_->http.add(HTTPMethod::GET,
                   "/api/v1/lobbies",
                   {
                       .summary = "List all lobbies",
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<LobbiesResponse>()}},
                                                {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_Lobbies(req, socket); },
                   });

  state_->http.add(HTTPMethod::POST,
                   "/api/v1/lobbies/create",
                   {
                       .summary = "Create a new lobby",
                       .request_description = APIDescription{.json_schema = rfl::json::to_schema<CreateLobbyRequest>()},
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<LobbyCreateResponse>()}},
                                                {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_LobbyCreate(req, socket); },
                   });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/lobbies/join",
      {
          .summary = "Join a lobby",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<events::JoinLobbyEvent>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_LobbyJoin(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/lobbies/leave",
      {
          .summary = "Leave a lobby",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<events::LeaveLobbyEvent>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_LobbyLeave(req, socket); },
      });

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/lobbies/stop",
      {
          .summary = "Stop a lobby",
          .request_description = APIDescription{.json_schema = rfl::json::to_schema<events::StopLobbyEvent>()},
          .response_description = {{200, {.json_schema = rfl::json::to_schema<GenericSuccessResponse>()}},
                                   {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_LobbyStop(req, socket); },
      });

  /**
   * Utils
   */
  state_->http.add(HTTPMethod::GET,
                   "/api/v1/utils/get-icon",
                   {.summary = "Get the icon for a given app",
                    .description = "Get the icon for a given app, pass the icon_path as a query parameter ex: "
                                   "/api/v1/utils/get-icon?icon_path=/etc/wolf/icons/steam.png",
                    .handler = [this](auto req, auto socket) { endpoint_GetIcon(req, socket); }});

  state_->http.add(
      HTTPMethod::GET,
      "/api/v1/docker/images/inspect",
      {.summary = "Inspect a Docker image",
       .description = "Inspect a Docker image and returns the full JSON response as is from the Docker APIs at "
                      "/images/{image_name}/json expects image_name as a query parameter.",
       .handler = [this](auto req, auto socket) { endpoint_DockerInspectImage(req, socket); }});

  state_->http.add(
      HTTPMethod::POST,
      "/api/v1/docker/images/pull",
      {.summary = "Pull a Docker image",
       .description = "Pull a Docker image, will keep the connection open to send back progress updates. Each "
                      "progress event will be a single line encoded as JSON.",
       .request_description = APIDescription{.json_schema = rfl::json::to_schema<DockerPullImageRequest>()},
       .response_description = {{200, {.json_schema = rfl::json::to_schema<DockerPullImageResponse>()}},
                                {500, {.json_schema = rfl::json::to_schema<GenericErrorResponse>()}}},
       .handler = [this](auto req, auto socket) { endpoint_DockerPullImage(req, socket); }});

  /**
   * OpenAPI schema
   */

  state_->http.add(HTTPMethod::GET,
                   "/api/v1/openapi-schema",
                   {.summary = "Return this OpenAPI schema as JSON", .handler = [this](auto req, auto socket) {
                      send_http(socket, 200, state_->http.openapi_schema());
                    }});

  state_->sse_keepalive_timer.async_wait([this](auto e) { sse_keepalive(e); });
  start_accept();
}

void UnixSocketServer::sse_keepalive(const boost::system::error_code &e) {
  if (e && e.value() != boost::asio::error::operation_aborted) {
    logs::log(logs::warning, "[API] Error in keepalive timer: {}", e.message());
    return;
  }
  cleanup_sockets();
  sse_broadcast(":keepalive\n\n");
  state_->sse_keepalive_timer.expires_after(SSE_KEEPALIVE_INTERVAL);
  state_->sse_keepalive_timer.async_wait([this](auto e) { sse_keepalive(e); });
}

void UnixSocketServer::sse_broadcast(const std::string &payload) {
  for (auto &socket : state_->sockets) {
    boost::asio::async_write(socket->socket,
                             boost::asio::buffer(payload),
                             [this, socket](const boost::system::error_code &ec, std::size_t /*length*/) {
                               if (ec) {
                                 logs::log(logs::debug, "[API] Error sending event: {}", ec.message());
                                 close(*socket);
                                 cleanup_sockets();
                               }
                             });
  }
}

void UnixSocketServer::broadcast_event(const std::string &event_type, const std::string &event_data) {
  sse_broadcast(fmt::format("event: {}\ndata: {}\n\n", event_type, event_data));
}

void UnixSocketServer::cleanup_sockets() {
  state_->sockets.erase(std::remove_if(state_->sockets.begin(),
                                       state_->sockets.end(),
                                       [](const auto &socket) { return !socket->is_alive; }),
                        state_->sockets.end());
}

void UnixSocketServer::send_http(std::shared_ptr<UnixSocket> socket, int status_code, std::string_view body) {
  send_http(socket, status_code, {{"Content-Length: " + std::to_string(body.size())}}, body);
}

void UnixSocketServer::send_http(std::shared_ptr<UnixSocket> socket,
                                 int status_code,
                                 const std::vector<std::string_view> &http_headers,
                                 std::string_view body) {
  auto http_reply = fmt::format("HTTP/1.0 {} OK\r\n{}\r\n\r\n{}", status_code, fmt::join(http_headers, "\r\n"), body);
  send_data(socket, http_reply);
}

void UnixSocketServer::send_data(std::shared_ptr<UnixSocket> socket, std::string_view data) {
  boost::asio::async_write(socket->socket,
                           boost::asio::buffer(data),
                           [this, socket](const boost::system::error_code &ec, std::size_t /*length*/) {
                             if (ec) {
                               logs::log(logs::error, "[API] Error sending data: {}", ec.message());
                               close(*socket);
                             }
                           });
}

void UnixSocketServer::handle_request(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  logs::log(logs::debug, "[API] Received request: {} {} - {}", rfl::enum_to_string(req.method), req.path, req.body);

  if (!state_->http.handle_request(req, socket)) {
    send_http(socket, 404, "");
    close(*socket);
  }
}

void UnixSocketServer::start_connection(std::shared_ptr<UnixSocket> socket) {
  auto request_buf = std::make_shared<boost::asio::streambuf>(std::numeric_limits<std::size_t>::max());
  boost::asio::async_read_until(
      socket->socket,
      *request_buf,
      "\r\n\r\n",
      [this, socket, request_buf](const boost::system::error_code &ec, std::size_t bytes_transferred) {
        if (ec) {
          logs::log(logs::error, "[API] Error reading request: {}", ec.message());
          close(*socket);
          return;
        }
        HTTPRequest req = {};
        std::string method;
        auto headers = to_str(*request_buf, bytes_transferred);
        std::istringstream is(headers);
        SimpleWeb::RequestMessage::parse(is, method, req.path, req.query_string, req.http_version, req.headers);
        if (method == "GET")
          req.method = HTTPMethod::GET;
        else if (method == "POST")
          req.method = HTTPMethod::POST;
        else if (method == "PUT")
          req.method = HTTPMethod::PUT;
        else if (method == "DELETE")
          req.method = HTTPMethod::DELETE;
        else
          req.method = HTTPMethod::GET;

        if (req.headers.contains("Transfer-Encoding") && req.headers.find("Transfer-Encoding")->second == "chunked") {
          logs::log(logs::error, "[API] Chunked encoding not supported, use HTTP/1.0 instead");
          send_http(socket, 500, "Chunked encoding not supported, use HTTP/1.0 instead");
          return;
        }

        // Get the body payload
        if (req.headers.contains("Content-Length")) {
          auto content_length = std::stoul(req.headers.find("Content-Length")->second);
          std::size_t num_additional_bytes = request_buf->size() - bytes_transferred;
          if (content_length > request_buf->max_size()) {
            send_http(socket, 413, "Payload Too Large");
            return;
          } else if (content_length > num_additional_bytes) {
            boost::asio::async_read(socket->socket,
                                    *request_buf,
                                    boost::asio::transfer_exactly(content_length - num_additional_bytes),
                                    [this,
                                     content_length,
                                     bytes_transferred,
                                     socket,
                                     request_buf,
                                     req = std::make_unique<HTTPRequest>(req)](const boost::system::error_code &ec,
                                                                               std::size_t /*bytes_transferred*/) {
                                      if (ec) {
                                        logs::log(logs::error, "[API] Error reading request body: {}", ec.message());
                                        close(*socket);
                                        return;
                                      }
                                      req->body = to_str(*request_buf, bytes_transferred, content_length);
                                      handle_request(*req, socket);
                                    });
          } else {
            req.body = to_str(*request_buf, bytes_transferred, content_length);
            handle_request(req, socket);
          }
        } else {
          handle_request(req, socket);
        }
      });
}

void UnixSocketServer::start_accept() {
  auto socket = std::make_shared<UnixSocket>(
      UnixSocket{.socket = boost::asio::local::stream_protocol::socket(state_->io_context)});
  state_->acceptor.async_accept(socket->socket, [this, socket](const boost::system::error_code &ec) {
    if (!ec) {
      start_accept();           // Immediately start accepting a new connection
      start_connection(socket); // Start reading the request from the new connection
    } else {
      logs::log(logs::error, "[API] Error accepting connection: {}", ec.message());
      close(*socket);
    }
  });
}

void UnixSocketServer::close(UnixSocket &socket) {
  socket.socket.close();
  socket.is_alive = false;
}
} // namespace wolf::api