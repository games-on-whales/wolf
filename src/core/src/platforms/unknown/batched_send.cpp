#include <core/batched_send.hpp>
#include <helpers/logger.hpp>

namespace wolf::platform {

bool send_batch(batched_send_info_t &send_info) {
  logs::log(logs::warning, "Batched send unsupported for the current platform.");
  return false;
}

bool send_single(int native_socket,
                 const boost::asio::ip::address &target_address,
                 uint16_t target_port,
                 const char *data,
                 size_t size) {
  logs::log(logs::warning, "UDP send unsupported for the current platform.");
  return false;
}

void configure_socket_for_streaming(boost::asio::ip::udp::socket &socket, bool is_video) {
  logs::log(logs::warning, "Socket configuration unsupported for the current platform.");
}

void enable_socket_qos(int native_socket, bool is_video) {
  logs::log(logs::warning, "QoS unsupported for the current platform.");
}

} // namespace wolf::platform
