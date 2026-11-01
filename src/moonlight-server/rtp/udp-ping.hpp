#pragma once

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <events/events.hpp>

namespace rtp {

using boost::asio::ip::udp;

struct RTPPingEvent {
  std::string client_ip;
  unsigned short client_port;
  /**
   * Moonlight protocol extension to support IP-less clients
   * will send back what was originally exchanged over RTSP
   * (see: X-SS-Ping-Payload and X-SS-Connect-Data)
   */
  std::optional<std::array<char, 16>> payload;
};

using on_rtp_ping_fn = std::function<void(const RTPPingEvent &)>;

/**
 * Generic UDP server, adapted from:
 * https://www.boost.org/doc/libs/1_81_0/doc/html/boost_asio/tutorial/tutdaytime6/src.html
 */
class UDP_Server : public boost::enable_shared_from_this<UDP_Server> {
public:
  UDP_Server(std::shared_ptr<udp::socket> socket, const on_rtp_ping_fn &callback)
      : socket_(std::move(socket)), callback(callback) {
    start_receive();
  };

private:
  void start_receive();
  void handle_receive(const boost::system::error_code &error, std::size_t /*bytes_transferred*/);

  std::shared_ptr<udp::socket> socket_;
  udp::endpoint remote_endpoint_;
  boost::array<char, 2048> recv_buffer_{};
  on_rtp_ping_fn callback;
};

void start_rtp_ping(unsigned short video_port,
                    unsigned short audio_port,
                    std::shared_ptr<wolf::core::events::EventBusType> event_bus);

} // namespace rtp