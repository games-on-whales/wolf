#include <api/api.hpp>
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <events/reflectors.hpp>
#include <helpers/tsqueue.hpp>
#include <memory>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <server_http.hpp>

namespace wolf::api {

using namespace wolf::core;

std::string to_str(boost::asio::streambuf &streambuf) {
  return {buffers_begin(streambuf.data()), buffers_end(streambuf.data())};
}

UnixSocketServer::UnixSocketServer(boost::asio::io_context &io_context,
                                   const std::string &socket_path,
                                   immer::box<state::AppState> app_state)
    : io_context_(io_context), app_state(app_state),
      acceptor_(io_context, boost::asio::local::stream_protocol::endpoint(socket_path)) {

  http.add(HTTPMethod::GET,
           "/api/v1/events",
           {.summary = "Subscribe to events",
            .description = "This endpoint allows clients to subscribe to events using HTTP 0.9.  \n"
                           "Events are sent as JSON objects, one per line. Clients should expect to receive a newline "
                           "character after each event. The connection will be kept open until the client closes it.",
            .handler = [this](auto req, auto socket) { endpoint_Events(req, socket); }});

  http.add(
      HTTPMethod::GET,
      "/api/v1/pending-pair-requests",
      {
          .summary = "Get pending pair requests",
          .description = "This endpoint returns a list of Moonlight clients that are currently waiting to be paired.",
          .response_description = {{200, {.json_schema = rfl::json::to_schema<PendingPairRequestsResponse>()}}},
          .handler = [this](auto req, auto socket) { endpoint_PendingPairRequest(req, socket); },
      });

  http.add(HTTPMethod::POST,
           "/api/v1/pair-client",
           {
               .summary = "Pair a client",
               .request_description = APIDescription{.json_schema = rfl::json::to_schema<PairRequest>()},
               .response_description = {{200, {.json_schema = rfl::json::to_schema<PairResponse>()}},
                                        {500, {.json_schema = rfl::json::to_schema<PairResponseError>()}}},
               .handler = [this](auto req, auto socket) { endpoint_Pair(req, socket); },
           });

  auto openapi_schema = http.openapi_schema();
  http.add(HTTPMethod::GET, "/api/v1/openapi-schema", {.handler = [this, openapi_schema](auto req, auto socket) {
             send_http(socket, 200, openapi_schema);
           }});

  start_accept();
}

void UnixSocketServer::broadcast_event(const std::string &event_json) {
  logs::log(logs::trace, "[API] Sending event: {}", event_json);
  for (auto &socket : sockets_) {
    boost::asio::async_write(socket->socket,
                             boost::asio::buffer(event_json + "\n"),
                             [this, socket](const boost::system::error_code &ec, std::size_t /*length*/) {
                               if (ec) {
                                 logs::log(logs::error, "[API] Error sending event: {}", ec.message());
                                 close(*socket);
                               }
                             });
  }
}

void UnixSocketServer::cleanup_sockets() {
  sockets_.erase(std::remove_if(sockets_.begin(), sockets_.end(), [](const auto &socket) { return !socket->is_alive; }),
                 sockets_.end());
}

void UnixSocketServer::send_http(std::shared_ptr<UnixSocket> socket, int status_code, std::string_view body) {
  auto http_reply = fmt::format("HTTP/1.0 {} OK\r\nContent-Length: {}\r\n\r\n{}", status_code, body.size(), body);
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

  if (!http.handle_request(req, socket)) {
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
          close(*socket);
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
  auto socket =
      std::make_shared<UnixSocket>(UnixSocket{.socket = boost::asio::local::stream_protocol::socket(io_context_)});
  acceptor_.async_accept(socket->socket, [this, socket](const boost::system::error_code &ec) {
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

void start_server(immer::box<state::AppState> app_state) {
  auto socket_path = "/tmp/wolf.sock";
  logs::log(logs::info, "Starting API server on {}", socket_path);

  auto event_queue = std::make_shared<TSQueue<events::EventsVariant>>();

  auto global_ev_handler = app_state->event_bus->register_global_handler([event_queue](events::EventsVariant ev) {
    /**
     * We could do server.send_event() here, but since this callback will be running from the fire_event() thread,
     * we don't want to block it. Instead, we'll push the event to a queue and have a separate thread handle the
     * sending of the event.
     */
    event_queue->push(std::move(ev));
  });

  ::unlink(socket_path);
  boost::asio::io_context io_context;
  UnixSocketServer server(io_context, socket_path, app_state);

  while (true) {
    io_context.run_for(std::chrono::milliseconds(100));
    server.cleanup_sockets();
    while (auto ev = event_queue->pop(std::chrono::milliseconds(0))) {
      std::visit([&server](auto &&arg) { server.broadcast_event(rfl::json::write(*arg)); }, *ev);
    }
  }
}

} // namespace wolf::api