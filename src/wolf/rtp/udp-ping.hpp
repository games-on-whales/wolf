#pragma once

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <helpers/logger.hpp>

namespace rtp {

using boost::asio::ip::udp;

void wait_for_ping(
    unsigned short port,
    const std::function<void(unsigned short /* client_port */, const std::string & /* client_ip */)> &callback);

} // namespace rtp