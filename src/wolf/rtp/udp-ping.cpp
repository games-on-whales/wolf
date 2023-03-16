#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <rtp/udp-ping.hpp>

namespace rtp {

/**
 * Generic UDP server, adapted from:
 * https://www.boost.org/doc/libs/1_81_0/doc/html/boost_asio/tutorial/tutdaytime6/src.html
 */
class udp_server : public boost::enable_shared_from_this<udp_server> {
public:
  udp_server(boost::asio::io_context &io_context,
             unsigned short port,
             const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback)
      : socket_(io_context, udp::endpoint(udp::v4(), port)), callback(callback) {
    start_receive();
  }

private:
  void start_receive() {
    socket_.async_receive_from(boost::asio::buffer(recv_buffer_),
                               remote_endpoint_,
                               boost::bind(&udp_server::handle_receive,
                                           this,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
  }

  void handle_receive(const boost::system::error_code &error, std::size_t /*bytes_transferred*/) {
    if (!error) {
      auto client_ip = remote_endpoint_.address().to_string();
      auto client_port = remote_endpoint_.port();

      logs::log(logs::trace, "[RTP] Received ping from {}:{}", client_ip, client_port);
      callback(client_port, client_ip);

      start_receive();
    }
  }

  udp::socket socket_;
  udp::endpoint remote_endpoint_;
  boost::array<char, 1> recv_buffer_{};
  std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> callback;
};

void wait_for_ping(
    unsigned short port,
    const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback) {
  try {
    boost::asio::io_context io_context;
    udp_server server(io_context, port, callback);

    logs::log(logs::info, "RTP server started on port: {}", port);

    io_context.run();
  } catch (std::exception &e) {
    logs::log(logs::warning, "[RTP] Unable to start RTP server: {}", e.what());
  }
}

} // namespace rtp