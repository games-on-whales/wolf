#pragma once

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <helpers/logger.hpp>

namespace rtp {

using boost::asio::ip::udp;

/**
 * Generic UDP server, adapted from:
 * https://www.boost.org/doc/libs/1_81_0/doc/html/boost_asio/tutorial/tutdaytime6/src.html
 */
class UDP_Server : public boost::enable_shared_from_this<UDP_Server> {
public:
  UDP_Server(
      unsigned short port,
      const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback);

  ~UDP_Server();

  void run(std::chrono::milliseconds timeout);

private:
  void start_receive();
  void handle_receive(const boost::system::error_code &error, std::size_t /*bytes_transferred*/);

  boost::asio::io_context io_context;
  udp::socket socket_;
  udp::endpoint remote_endpoint_;
  boost::array<char, 1> recv_buffer_{};
  std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> callback;
};

void wait_for_ping(
    unsigned short port,
    const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback);

} // namespace rtp