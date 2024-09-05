#include <api/api.hpp>
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <events/events.hpp>
#include <events/reflectors.hpp>
#include <helpers/tsqueue.hpp>
#include <memory>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <server_http.hpp>

namespace wolf::api {

using namespace wolf::core;

struct UnixSocket {
  boost::asio::local::stream_protocol::socket socket;
  bool is_alive = true;
};

struct HTTPRequest {
  std::string method{};
  std::string path{};
  std::string query_string{};
  std::string http_version{};
  SimpleWeb::CaseInsensitiveMultimap headers{};
};

std::string to_str(boost::asio::streambuf &streambuf) {
  return {buffers_begin(streambuf.data()), buffers_end(streambuf.data())};
}

class UnixSocketServer {
public:
  UnixSocketServer(boost::asio::io_context &io_context, const std::string &socket_path)
      : io_context_(io_context), acceptor_(io_context, boost::asio::local::stream_protocol::endpoint(socket_path)) {
    start_accept();
  }

  void send_event(const std::string &event_json) {
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

  void cleanup_sockets() {
    sockets_.erase(
        std::remove_if(sockets_.begin(), sockets_.end(), [](const auto &socket) { return !socket->is_alive; }),
        sockets_.end());
  }

private:
  void handle_request(HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
    logs::log(logs::debug, "[API] Received request: {} {}", req.method, req.path);
    if (req.method == "GET" && req.path == "/api/v1/events") {
      // Test with: curl -v --http0.9 --unix-socket /tmp/wolf.sock http://localhost/api/v1/events
      sockets_.push_back(socket);
    } else {
      logs::log(logs::warning, "[API] Invalid request: {} {}", req.method, req.path);
      close(*socket);
    }
  }

  void start_connection(std::shared_ptr<UnixSocket> socket) {
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
          std::istream is(request_buf.get());
          SimpleWeb::RequestMessage::parse(is, req.method, req.path, req.query_string, req.http_version, req.headers);
          handle_request(req, socket);
        });
  }

  void start_accept() {
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

  void close(UnixSocket &socket) {
    socket.socket.close();
    socket.is_alive = false;
  }

  boost::asio::io_context &io_context_;
  boost::asio::local::stream_protocol::acceptor acceptor_;
  std::vector<std::shared_ptr<UnixSocket>> sockets_;
};

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
  UnixSocketServer server(io_context, socket_path);

  while (true) {
    io_context.run_for(std::chrono::milliseconds(100));
    server.cleanup_sockets();
    while (auto ev = event_queue->pop(std::chrono::milliseconds(0))) {
      std::visit([&server](auto &&arg) { server.send_event(rfl::json::write(*arg)); }, *ev);
    }
  }
}

} // namespace wolf::api