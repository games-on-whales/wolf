#pragma once

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <helpers/logger.hpp>

namespace rtp {

using boost::asio::ip::udp;

/**
 * It'll wait for a UDP request at the specified port.
 * Returns the port number that has been opened by the remote client
 */
unsigned short wait_for_ping(unsigned short port) {
  boost::asio::io_context io_context;
  udp::socket socket(io_context, udp::endpoint(udp::v4(), port));

  boost::array<char, 4> recv_buf{}; // Will send PING (4 chars)
  udp::endpoint remote_endpoint;

  // Stop over here until we receive from Moonlight
  // TODO: timeout?
  socket.receive_from(boost::asio::buffer(recv_buf), remote_endpoint);

  auto client_port = remote_endpoint.port();
  logs::log(logs::debug, "Received PING from {}:{}", remote_endpoint.address(), client_port);
  return client_port;
}

} // namespace rtp