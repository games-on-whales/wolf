#include <api/api.hpp>

namespace wolf::api {

using namespace wolf::core;

constexpr auto SSE_KEEPALIVE_INTERVAL = std::chrono::seconds(15);

std::string to_str(boost::asio::streambuf &streambuf) {
  return {buffers_begin(streambuf.data()), buffers_end(streambuf.data())};
}

UnixSocketServer::UnixSocketServer(boost::asio::io_context &io_context,
                                   const std::string &socket_path,
                                   immer::box<state::AppState> app_state) {

  state_ = std::make_shared<UnixSocketState>(
      UnixSocketState{.io_context = io_context,
                      .app_state = app_state,
                      .acceptor = {io_context, boost::asio::local::stream_protocol::endpoint(socket_path)},
                      .http = HTTPServer<std::shared_ptr<UnixSocket>>{},
                      .sse_keepalive_timer = boost::asio::steady_timer{io_context}});

  state_->http.add(
      HTTPMethod::GET,
      "/api/v1/events",
      {.summary = "Subscribe to events",
       .description =
           "This endpoint allows clients to subscribe to events using "
           "[SSE](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events).  \n",
       // TODO: json_schema = rfl::json::to_schema<EventsVariant>()
       .handler = [this](auto req, auto socket) { endpoint_Events(req, socket); }});

  state_->http.add(
      HTTPMethod::GET,
      "/api/v1/pending-pair-requests",
      {
          .summary = "Get pending pair requests",
          .description = "This endpoint returns a list of Moonlight clients that are currently waiting to be paired.",
          .response_description = {{200, {.json_schema = rfl::json::to_schema<PendingPairRequestsResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_PendingPairRequest(req, socket); },
      });

  state_->http.add(HTTPMethod::POST,
                   "/api/v1/pair-client",
                   {
                       .summary = "Pair a client",
                       .request_description = APIDescription{.json_schema = rfl::json::to_schema<PairRequest>()},
                       .response_description = {{200, {.json_schema = rfl::json::to_schema<PairResponse>()}},
                                                {500, {.json_schema = rfl::json::to_schema<PairResponseError>()}}},
                       .handler = [this](auto req, auto socket) { endpoint_Pair(req, socket); },
                   });

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
  state_->sse_keepalive_timer.expires_from_now(SSE_KEEPALIVE_INTERVAL);
  state_->sse_keepalive_timer.async_wait([this](auto e) { sse_keepalive(e); });
}

void UnixSocketServer::sse_broadcast(const std::string &payload) {
  for (auto &socket : state_->sockets) {
    boost::asio::async_write(socket->socket,
                             boost::asio::buffer(payload),
                             [this, socket](const boost::system::error_code &ec, std::size_t /*length*/) {
                               if (ec) {
                                 logs::log(logs::warning, "[API] Error sending event: {}", ec.message());
                                 close(*socket);
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
  boost::asio::async_write(socket->socket,
                           boost::asio::buffer(http_reply),
                           [this, socket](const boost::system::error_code &ec, std::size_t /*length*/) {
                             if (ec) {
                               logs::log(logs::error, "[API] Error sending HTTP: {}", ec.message());
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
        std::istream is(request_buf.get());
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
          if (content_length > num_additional_bytes) {
            boost::asio::async_read(socket->socket,
                                    *request_buf,
                                    boost::asio::transfer_exactly(content_length - bytes_transferred),
                                    [this, socket, request_buf, req = std::make_unique<HTTPRequest>(req)](
                                        const boost::system::error_code &ec,
                                        std::size_t /*bytes_transferred*/) {
                                      if (ec) {
                                        logs::log(logs::error, "[API] Error reading request body: {}", ec.message());
                                        close(*socket);
                                        return;
                                      }
                                      req->body = to_str(*request_buf);
                                      handle_request(*req, socket);
                                    });
          } else {
            req.body = to_str(*request_buf);
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