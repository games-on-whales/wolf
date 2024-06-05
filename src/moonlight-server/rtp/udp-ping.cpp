#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <rtp/udp-ping.hpp>
#include <thread>

namespace rtp {

UDP_Server::UDP_Server(
    unsigned short port,
    const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback)
    : io_context(), socket_(io_context, udp::endpoint(udp::v4(), port)), callback(callback) {
  // We have to enable this because we'll bind additional sockets as udpsink in the audio/video pipelines
  socket_.set_option(udp::socket::reuse_address(true));
  start_receive();
}

UDP_Server::~UDP_Server() {
  socket_.close();
  io_context.stop();
}

void UDP_Server::run(std::chrono::milliseconds timeout) {
  io_context.run_for(timeout);
}

void UDP_Server::start_receive() {
  socket_.async_receive_from(boost::asio::buffer(recv_buffer_),
                             remote_endpoint_,
                             boost::bind(&UDP_Server::handle_receive,
                                         this,
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred));
}

void UDP_Server::handle_receive(const boost::system::error_code &error, std::size_t /*bytes_transferred*/) {
  if (!error) {
    auto client_ip = remote_endpoint_.address().to_string();
    auto client_port = remote_endpoint_.port();

    logs::log(logs::trace, "[RTP] Received ping from {}:{}", client_ip, client_port);
    callback(client_port, client_ip);
    // We'll keep receiving pings and sending callback events until the timeout elapsed.
    // This is because we don't know if downstream they are ready to start the session.
    // Downstream will make sure to only send one ping per session
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // let's avoid spamming though
    start_receive();
  }
}

void wait_for_ping(
    unsigned short port,
    const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback) {
  auto thread = std::thread(
      [port](
          const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback) {
        try {
          auto server = UDP_Server(port, callback);

          logs::log(logs::info, "RTP server started on port: {}", port);
          server.run(std::chrono::seconds(4));
          logs::log(logs::debug, "RTP server on port: {} stopped", port);

        } catch (std::exception &e) {
          logs::log(logs::warning, "[RTP] Unable to start RTP server on {}: {}", port, e.what());
        }
      },
      callback);
  thread.detach();
}

} // namespace rtp