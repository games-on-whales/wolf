#include "boost/algorithm/hex.hpp"
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <rtsp/commands.hpp>
#include <string_view>
#include <thread>

namespace rtsp {

namespace asio = boost::asio;
using asio::ip::tcp;
using namespace std::string_view_literals;

/**
 * A wrapper on top of the basic boost socket
 * see the tutorial at: https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/tutorial/tutdaytime3.html
 */
class tcp_connection : public boost::enable_shared_from_this<tcp_connection> {
public:
  typedef boost::shared_ptr<tcp_connection> pointer;

  static pointer create(boost::asio::io_context &io_context, immer::box<state::StreamSession> state) {
    return pointer(new tcp_connection(io_context, state));
  }

  auto &socket() {
    return socket_;
  }

  /**
   * Will start the following (async) chain:
   *  1- wait for a message
   *  2- once received, parse it
   *  3- call the corresponding rtsp::command
   *  4- send back the response message
   */
  void start() {
    logs::log(logs::trace, "[RTSP] received connection from IP: {}", socket().remote_endpoint().address().to_string());
    receive_message([self = shared_from_this()](auto raw_message, auto bytes_transferred) {
      if (auto parsed_msg = self->interpret_message(raw_message, bytes_transferred)) {
        auto response = commands::message_handler(std::move(parsed_msg.value()), self->stream_session);
        self->send_message(std::move(response), [](auto bytes) {});
      } else {
        logs::log(logs::error, "[RTSP] error parsing message: {}", raw_message);
        self->send_message(std::move(create_error_msg(400, "BAD REQUEST")), [](auto bytes) {});
      }
    });
  }

  /**
   * We have no way to know the size of the message (no content-length param) and there's no special sequence of
   * characters to delimit the end.
   * So we'll try to read everything that has been sent, we enforce a max message size AND a timeout in order to avoid
   * stalling.
   *
   * Timeout is adapted from:
   * https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/example/cpp11/timeouts/async_tcp_client.cpp
   *
   * @return Will call the callback passing a string representation of the message and the transferred bytes
   */
  void
  receive_message(std::function<void(std::string_view /* raw_message */, int /* bytes_transferred */)> on_msg_read) {

    asio::steady_timer deadline_(ioc);

    deadline_.async_wait([&deadline_, self = shared_from_this()](auto error) {
      if (!error && deadline_.expiry() <= asio::steady_timer::clock_type::now()) { // The deadline has passed
        logs::log(logs::trace, "[RTSP] deadline over");
        self->socket_.cancel();
        deadline_.cancel();
      }
    });
    deadline_.expires_after(std::chrono::milliseconds(timeout_millis));

    boost::asio::async_read(
        socket(),
        streambuf_,
        boost::asio::transfer_at_least(1),
        [self = shared_from_this(), on_msg_read, &deadline_](auto error_code, auto bytes_transferred) {
          if (error_code &&
              error_code != boost::asio::error::operation_aborted) { // it'll be aborted when the deadline expires
            logs::log(logs::error, "[RTSP] error during transmission: {}", error_code.message());
            self->send_message(std::move(create_error_msg(400, "BAD REQUEST")), [](auto bytes) {});
            return;
          }
          deadline_.cancel();
          auto raw_msg = self->append_stream_to_buffer(bytes_transferred);
          on_msg_read(raw_msg, bytes_transferred);
        });
  }

  /**
   * APPEND the current transferred data to the msg_buffer,
   * this is needed because some messages are multipart, see the recursion at `interpret_message()`
   *
   * side effects:
   *  - streambuf_ will be read and cleaned out after
   *  - msg_buffer_ will append streambuf_ content
   *  - buffer_size will be incremented by bytes_transferred
   *
   *  @returns a string representation of the resulting msg_buffer
   */
  std::string_view append_stream_to_buffer(int bytes_transferred) {
    boost::asio::buffer_copy(boost::asio::buffer(&msg_buffer_.front() + buffer_size, bytes_transferred),
                             streambuf_.data(),
                             bytes_transferred);
    streambuf_.consume(bytes_transferred); // clear up the streambuffer
    buffer_size += bytes_transferred;
    return {msg_buffer_.data(), (size_t)buffer_size};
  }

  /**
   * Given a raw sequence of characters read from the socket we'll parse it in a `msg_t` structure
   *
   * @returns the parsed msg (if possible and valid), null otherwise
   */
  static std::optional<msg_t> interpret_message(std::string_view raw_msg, int bytes_transferred) {
    logs::log(logs::trace, "[RTSP] read message {} bytes \n{}", bytes_transferred, raw_msg);
    if (auto parsed_msg = parse_rtsp_msg(raw_msg, bytes_transferred)) {
      return std::move(parsed_msg.value());
    } else {
      return {};
    }
  }

  /**
   * Will fully write back the given message to the socket
   * calls on_sent(bytes_transferred) when over
   */
  void send_message(msg_t response, std::function<void(int /* bytes_transferred */)> on_sent) {
    auto raw_response = serialize_rtsp_msg(std::move(response));
    logs::log(logs::trace, "[RTSP] sending reply: \n{}", raw_response);
    boost::asio::async_write(socket(),
                             boost::asio::buffer(raw_response),
                             [on_sent](auto error_code, auto bytes_transferred) {
                               if (error_code) {
                                 logs::log(logs::error, "[RTSP] error during transmission: {}", error_code.message());
                               }
                               logs::log(logs::trace, "[RTSP] sent reply of size: {}", bytes_transferred);
                               on_sent(bytes_transferred);
                             });
  }

protected:
  explicit tcp_connection(boost::asio::io_context &io_context, immer::box<state::StreamSession> state)
      : socket_(io_context), ioc(io_context), buffer_size(0), msg_buffer_(max_msg_size), streambuf_(max_msg_size),
        stream_session(std::move(state)) {}
  tcp::socket socket_;

  static constexpr auto max_msg_size = 2048;
  static constexpr auto timeout_millis = 1500;

  boost::asio::io_context &ioc;
  std::vector<char> msg_buffer_;
  boost::asio::streambuf streambuf_;
  long buffer_size;

  immer::box<state::StreamSession> stream_session;
};

/**
 * A generic TCP server that accepts incoming client connections
 * adapted from the tutorial at: https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/tutorial/tutdaytime3.html
 */
class tcp_server {
public:
  tcp_server(boost::asio::io_context &io_context, int port, immer::box<state::StreamSession> state)
      : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        stream_session(std::move(state)) {
    acceptor_.set_option(boost::asio::socket_base::reuse_address{true});
    acceptor_.listen(4096);
    start_accept();
  }

private:
  /**
   * Creates a socket and initiates an asynchronous accept operation to wait for a new connection
   */
  void start_accept() {
    tcp_connection::pointer new_connection = tcp_connection::create(io_context_, stream_session);

    acceptor_.async_accept(new_connection->socket(),
                           [this, new_connection](auto error) { handle_accept(new_connection, error); });
  }

  /**
   * Called when the asynchronous accept operation initiated by start_accept() finishes. It services the client
   * request, and then calls start_accept() to initiate the next accept operation.
   */
  void handle_accept(const tcp_connection::pointer &new_connection, const boost::system::error_code &error) {
    if (!error) {
      new_connection->start();
    } else {
      logs::log(logs::error, "[RTSP] error during connection: {}", error.message());
    }

    start_accept();
  }

  boost::asio::io_context &io_context_;
  tcp::acceptor acceptor_;
  immer::box<state::StreamSession> stream_session;
};

std::thread start_server(int port, immer::box<state::StreamSession> state) {
  auto thread = std::thread([port, state = std::move(state)]() {
    try {
      boost::asio::io_context io_context;
      tcp_server server(io_context, port, state);

      logs::log(logs::info, "RTSP server started on port: {}", port);

      io_context.run();
    } catch (std::exception &e) {
      logs::log(logs::error, "Unable to create RTSP server on port: {} ex: {}", port, e.what());
    }
  });

  return thread;
}

} // namespace rtsp
