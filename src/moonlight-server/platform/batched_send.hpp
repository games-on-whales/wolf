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
  // Optional headers to be prepended to each packet (e.g., RTP + NV headers)
  const char *headers = nullptr;
  size_t header_size = 0;

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
  buffer_descriptor_t buffer_for_payload_offset(ptrdiff_t offset) const;
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
 * @brief Send a single UDP packet (fallback for non-batched sends)
 *
 * @param native_socket The native socket handle
 * @param target_address The target IP address
 * @param target_port The target port
 * @param data The data to send
 * @param size The size of the data
 * @return true if the packet was sent successfully
 * @return false if an error occurred
 */
bool send_single(int native_socket,
                 const boost::asio::ip::address &target_address,
                 uint16_t target_port,
                 const char *data,
                 size_t size);

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
