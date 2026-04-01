#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>
#include <cstdint>
#include <vector>

namespace wolf::platform {

/**
 * @brief Describes a buffer region for scatter-gather I/O
 */
struct buffer_descriptor_t {
  const char *buffer;
  size_t size;

  buffer_descriptor_t(const char *buffer, size_t size) : buffer(buffer), size(size) {}
  buffer_descriptor_t() : buffer(nullptr), size(0) {}
};

/**
 * @brief Information for sending a batch of UDP packets
 *
 * This structure is used to batch multiple UDP packets into a single
 * system call (sendmmsg on Linux, WSASendMsg on Windows) for improved
 * throughput and reduced CPU usage.
 */
struct batched_send_info_t {
  // One or more data buffers containing the payloads
  // These must be aligned to payload_size boundaries
  std::vector<buffer_descriptor_t> payload_buffers;
  size_t payload_size = 0;

  // The offset (in header+payload message blocks) to begin sending from
  size_t block_offset = 0;

  // Number of header+payload message blocks to send
  size_t block_count = 0;

  // The native socket handle (platform-specific)
  int native_socket = -1;

  // Target address and port
  boost::asio::ip::address target_address;
  uint16_t target_port = 0;

  // Source address (for IP_PKTINFO to ensure proper routing on multi-homed hosts)
  boost::asio::ip::address source_address;

  /**
   * @brief Returns a buffer descriptor for the given payload offset
   * @param offset The offset in the total payload data (bytes)
   * @return Buffer descriptor describing the region at the given offset
   */
  inline buffer_descriptor_t buffer_for_payload_offset(ptrdiff_t offset) const {
    for (const auto &desc : payload_buffers) {
      if (offset < (ptrdiff_t)desc.size) {
        return {
            desc.buffer + offset,
            desc.size - offset,
        };
      } else {
        offset -= desc.size;
      }
    }
    return {};
  }
};

/**
 * @brief Send a batch of UDP packets using scatter-gather I/O
 *
 * This function sends multiple UDP packets in a single system call,
 * significantly reducing CPU overhead compared to sending packets
 * individually.
 *
 * @param send_info The batch send information
 * @return true if all packets were sent successfully
 * @return false if an error occurred
 */
bool send_batch(batched_send_info_t &send_info);

/**
 * @brief Configure socket for high-bandwidth streaming
 *
 * Sets socket options for optimal streaming performance:
 * - Increases send buffer size
 * - Enables non-blocking mode with proper EAGAIN handling
 * - Sets traffic priority
 *
 * @param socket The UDP socket to configure
 * @param is_video Whether this is a video socket (for priority)
 */
void configure_socket_for_streaming(boost::asio::ip::udp::socket &socket, bool is_video = true);

/**
 * @brief Enable QoS (DSCP tagging) on a socket
 *
 * @param native_socket The native socket handle
 * @param is_video Whether this is video traffic (CS5) or audio (CS6)
 */
void enable_socket_qos(int native_socket, bool is_video);

} // namespace wolf::platform
