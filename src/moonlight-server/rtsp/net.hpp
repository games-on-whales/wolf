#pragma once

#include "boost/algorithm/hex.hpp"
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <rtsp/commands.hpp>
#include <state/sessions.hpp>
#include <string_view>
#include <thread>
#include <utility>

namespace rtsp {

namespace asio = boost::asio;
using asio::ip::tcp;
using namespace std::string_view_literals;

/**
 * A wrapper on top of the basic boost socket, it'll be in charge of sending and receiving RTSP messages
 * based on the tutorial at: https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/tutorial/tutdaytime3.html
 *
 * Bear in mind that the basic methods of this class can be trivially used to also implement the RTSP client, see the
 * testRTSP class for an example client implementation.
 */
class tcp_connection : public boost::enable_shared_from_this<tcp_connection> {
public:
  typedef boost::shared_ptr<tcp_connection> pointer;

  static pointer create(boost::asio::io_context &io_context,
                        const state::SessionsAtoms &stream_sessions,
                        const std::shared_ptr<dp::event_bus> &event_bus) {
    return pointer(new tcp_connection(io_context, stream_sessions, event_bus));
  }

  auto &socket() {
    return socket_;
  }

  /**
   * Cleanly closing the socket, see: https://stackoverflow.com/questions/1993216/boostasio-cleanly-disconnecting
   *
   * Moonlight detects you're done writing the message by waiting for the TCP FIN that is generated when you gracefully
   * close your socket. It looks like Boost's default behavior for close() is that it performs an abortive close
   * which drops any outstanding data and sends a RST to the remote host if there's any unacked data remaining
   * in the send buffer.
   * So effectively, it's a race condition whether the client gets all the data and acks first or the socket is
   * closed and a RST packet is sent (which causes the ECONNRESET error).
   *
   * - @cgutman
   */
  void close() {
    logs::log(logs::trace, "[RTSP] closing socket");
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    socket_.close();
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
    receive_message([self = shared_from_this()](auto parsed_msg) {
      if (parsed_msg) {
        auto user_ip = self->socket().remote_endpoint().address().to_string();
        auto session = get_session_by_ip(self->stream_sessions->load(), user_ip);
        if (session) {
          auto response = commands::message_handler(parsed_msg.value(), session.value(), self->event_bus);
          self->send_message(response, [self](auto bytes) { self->close(); });
        } else {
          logs::log(logs::warning, "[RTSP] received packet from unrecognised client: {}", user_ip);
          self->close();
        }
      } else {
        logs::log(logs::error, "[RTSP] error parsing message");
        self->send_message((rtsp::commands::error_msg(400, "BAD REQUEST")), [self](auto bytes) { self->close(); });
      }
    });
  }

  /**
   * We have no way to know the size of the message and there's no special sequence of characters to delimit the very
   * end (including payload). So we'll try to read everything that has been sent, we enforce a max message size AND a
   * timeout in order to avoid stalling.
   *
   * Timeout is adapted from:
   * https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/example/cpp11/timeouts/async_tcp_client.cpp
   *
   * @return Will call the callback passing a string representation of the message and the transferred bytes
   *
   * @note: ANNOUNCE messages will be bigger than the default 512 bytes that boost reads, fortunately we receive an
   * option in the message: Content-length which represent the size in bytes of the payload. We'll keep recursively call
   * receive_message() until our payload size matches the specified Content-length
   */
  void receive_message(const std::function<void(std::optional<RTSP_PACKET>)> &on_msg_read) {
    deadline_.async_wait([self = shared_from_this()](auto error) {
      if (!error && self->deadline_.expiry() <= asio::steady_timer::clock_type::now()) { // The deadline has passed
        logs::log(logs::trace, "[RTSP] deadline over");
        self->socket_.cancel();
        self->deadline_.cancel();
      }
    });
    deadline_.expires_after(std::chrono::milliseconds(timeout_millis));

    boost::asio::async_read(
        socket(),
        streambuf_,
        boost::asio::transfer_at_least(1),
        [self = shared_from_this(), on_msg_read](auto error_code, auto bytes_transferred) {
          if (error_code &&
              error_code != boost::asio::error::operation_aborted) { // it'll be aborted when the deadline expires
            logs::log(logs::error, "[RTSP] error during transmission: {}", error_code.message());
            self->send_message(rtsp::commands::error_msg(400, "BAD REQUEST"), [](auto bytes) {});
            return;
          }
          self->deadline_.cancel(); // stop the deadline
          std::string raw_msg = {std::istreambuf_iterator<char>(&self->streambuf_), {}};
          logs::log(logs::trace, "[RTSP] received message {} bytes \n{}", bytes_transferred, raw_msg);

          auto full_raw_msg = self->prev_read_ + raw_msg;
          auto total_bytes_transferred = self->prev_read_bytes_ + bytes_transferred;
          full_raw_msg.resize(total_bytes_transferred);

          // We should be able to rtsp::parse() even when getting a partial message
          // Since we had a few issues with this approach in the past now I'm manually reading the "Content-length"
          // It's ugly, but it shouldn't be slower than parsing the whole message.
          auto content_length_pos = full_raw_msg.find("Content-length: ");
          if (content_length_pos != std::string::npos) {
            content_length_pos += 16; //"Content-length: "sv.size();
            auto content_lenght_end = full_raw_msg.find("\r\n", content_length_pos);
            auto total_length_str = full_raw_msg.substr(content_length_pos, content_lenght_end - content_length_pos);
            auto total_length = std::stoi(total_length_str);
            if (total_bytes_transferred < total_length) {
              self->prev_read_ = full_raw_msg;
              self->prev_read_bytes_ += bytes_transferred;
              return self->receive_message(on_msg_read);
            }
          }

          auto msg = rtsp::parse(full_raw_msg);
          self->prev_read_ = "";
          self->prev_read_bytes_ = 0;
          on_msg_read(msg);
        });
  }

  /**
   * Will fully write back the given message to the socket
   * calls on_sent(bytes_transferred) when over
   */
  void send_message(const rtsp::RTSP_PACKET &response,
                    const std::function<void(int /* bytes_transferred */)> &on_sent) {
    auto raw_response = rtsp::to_string(response);
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
  explicit tcp_connection(boost::asio::io_context &io_context,
                          state::SessionsAtoms stream_sessions,
                          const std::shared_ptr<dp::event_bus> &event_bus)
      : socket_(io_context), streambuf_(max_msg_size), deadline_(io_context),
        stream_sessions(std::move(stream_sessions)), prev_read_bytes_(0), event_bus(event_bus) {}
  tcp::socket socket_;

  static constexpr auto max_msg_size = 2048;
  static constexpr auto timeout_millis = 2500;

  boost::asio::streambuf streambuf_;
  asio::steady_timer deadline_;
  std::string prev_read_;
  int prev_read_bytes_;

  state::SessionsAtoms stream_sessions;
  std::shared_ptr<dp::event_bus> event_bus;
};

/**
 * A generic TCP server that accepts incoming client connections
 * adapted from the tutorial at: https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/tutorial/tutdaytime3.html
 */
class tcp_server {
public:
  tcp_server(boost::asio::io_context &io_context,
             int port,
             state::SessionsAtoms state,
             const std::shared_ptr<dp::event_bus> &event_bus)
      : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        stream_sessions(std::move(state)), event_bus(event_bus) {
    acceptor_.set_option(boost::asio::socket_base::reuse_address{true});
    acceptor_.listen(4096);
    start_accept();
  }

private:
  /**
   * Creates a socket and initiates an asynchronous accept operation to wait for a new connection
   */
  void start_accept() {
    tcp_connection::pointer new_connection = tcp_connection::create(io_context_, stream_sessions, event_bus);

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
  state::SessionsAtoms stream_sessions;
  std::shared_ptr<dp::event_bus> event_bus;
};

/**
 * Starts a new RTSP server, calling this method will block execution.
 */
void run_server(int port,
                const state::SessionsAtoms &running_sessions,
                const std::shared_ptr<dp::event_bus> &event_bus) {
  try {
    boost::asio::io_context io_context;
    tcp_server server(io_context, port, running_sessions, event_bus);

    logs::log(logs::info, "RTSP server started on port: {}", port);

    // This will block here until the context is stopped
    io_context.run();
  } catch (std::exception &e) {
    logs::log(logs::error, "Unable to create RTSP server on port: {} ex: {}", port, e.what());
  }
}

} // namespace rtsp
