#include <core/batched_send.hpp>
#include <helpers/logger.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace wolf::platform {

// Convert address_v4 to sockaddr_in
static struct sockaddr_in to_sockaddr_v4(const boost::asio::ip::address_v4 &address, uint16_t port) {
  struct sockaddr_in saddr = {};
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  auto bytes = address.to_bytes();
  memcpy(&saddr.sin_addr, bytes.data(), sizeof(saddr.sin_addr));
  return saddr;
}

// Convert address_v6 to sockaddr_in6
static struct sockaddr_in6 to_sockaddr_v6(const boost::asio::ip::address_v6 &address, uint16_t port) {
  struct sockaddr_in6 saddr = {};
  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);
  saddr.sin6_scope_id = address.scope_id();
  auto bytes = address.to_bytes();
  memcpy(&saddr.sin6_addr, bytes.data(), sizeof(saddr.sin6_addr));
  return saddr;
}

bool send_batch(batched_send_info_t &send_info) {
  if (send_info.native_socket < 0) {
    logs::log(logs::error, "Invalid socket in send_batch");
    return false;
  }

  auto sockfd = send_info.native_socket;
  struct msghdr msg_template = {};

  // Convert target address
  struct sockaddr_in taddr_v4 = {};
  struct sockaddr_in6 taddr_v6 = {};

  if (!send_info.target_address.is_unspecified()) {
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr_v6(send_info.target_address.to_v6(), send_info.target_port);
      msg_template.msg_name = &taddr_v6;
      msg_template.msg_namelen = sizeof(taddr_v6);
    } else {
      taddr_v4 = to_sockaddr_v4(send_info.target_address.to_v4(), send_info.target_port);
      msg_template.msg_name = &taddr_v4;
      msg_template.msg_namelen = sizeof(taddr_v4);
    }
  }

  // Prepare CMSG for source address (IP_PKTINFO/IPV6_PKTINFO)
  union {
    char buf[CMSG_SPACE(sizeof(struct in_pktinfo))];
    struct cmsghdr alignment;
  } cmbuf = {};

  socklen_t cmbuflen = 0;
  struct cmsghdr *pktinfo_cm = nullptr;

  if (!send_info.source_address.is_unspecified()) {
    msg_template.msg_control = cmbuf.buf;
    msg_template.msg_controllen = sizeof(cmbuf.buf);

    pktinfo_cm = CMSG_FIRSTHDR(&msg_template);

    if (send_info.source_address.is_v6()) {
      // IPv6
      struct in6_pktinfo pktInfo = {};
      auto src_v6 = send_info.source_address.to_v6();
      auto bytes = src_v6.to_bytes();
      memcpy(&pktInfo.ipi6_addr, bytes.data(), sizeof(pktInfo.ipi6_addr));
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += CMSG_SPACE(sizeof(pktInfo));
      pktinfo_cm->cmsg_level = IPPROTO_IPV6;
      pktinfo_cm->cmsg_type = IPV6_PKTINFO;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(pktInfo));
      memcpy(CMSG_DATA(pktinfo_cm), &pktInfo, sizeof(pktInfo));
    } else {
      // IPv4
      struct in_pktinfo pktInfo = {};
      auto src_v4 = send_info.source_address.to_v4();
      auto bytes = src_v4.to_bytes();
      memcpy(&pktInfo.ipi_spec_dst, bytes.data(), sizeof(pktInfo.ipi_spec_dst));
      pktInfo.ipi_ifindex = 0;

      cmbuflen += CMSG_SPACE(sizeof(pktInfo));
      pktinfo_cm->cmsg_level = IPPROTO_IP;
      pktinfo_cm->cmsg_type = IP_PKTINFO;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(pktInfo));
      memcpy(CMSG_DATA(pktinfo_cm), &pktInfo, sizeof(pktInfo));
    }
  }

  // Build mmsghdr array for sendmmsg
  const size_t MAX_BATCH = 64; // Limit to avoid stack overflow
  size_t remaining_blocks = send_info.block_count;
  size_t blocks_sent = 0;

  while (remaining_blocks > 0) {
    size_t batch_size = std::min(remaining_blocks, MAX_BATCH);

    struct mmsghdr msgs[MAX_BATCH];
    struct iovec iovs[MAX_BATCH * 2]; // Header + payload for each
    int iov_idx = 0;

    // Build the batch
    for (size_t i = 0; i < batch_size; i++) {
      size_t block_idx = send_info.block_offset + blocks_sent + i;

      msgs[i].msg_len = 0;
      msgs[i].msg_hdr.msg_iov = &iovs[iov_idx];
      msgs[i].msg_hdr.msg_iovlen = 1;
      msgs[i].msg_hdr.msg_name = msg_template.msg_name;
      msgs[i].msg_hdr.msg_namelen = msg_template.msg_namelen;
      msgs[i].msg_hdr.msg_control = msg_template.msg_control;
      msgs[i].msg_hdr.msg_controllen = cmbuflen;
      msgs[i].msg_hdr.msg_flags = 0;

      // Add payload - support both fixed-size and variable-sized buffers
      if (send_info.payload_size > 0 && send_info.payload_buffers.size() == 1) {
        // Fixed-size mode: single buffer with payload_size blocks
        auto payload_offset = block_idx * send_info.payload_size;
        auto payload_desc = send_info.buffer_for_payload_offset(payload_offset);
        iovs[iov_idx].iov_base = (void *)payload_desc.buffer;
        iovs[iov_idx].iov_len = send_info.payload_size;
      } else {
        // Variable-size mode: each payload_buffers entry is one packet
        const auto &payload_desc = send_info.payload_buffers[block_idx];
        iovs[iov_idx].iov_base = (void *)payload_desc.buffer;
        iovs[iov_idx].iov_len = payload_desc.size;
      }
      iov_idx++;
    }

    // Send the batch using sendmmsg
    int retries = 0;
    size_t batch_sent = 0;

    while (batch_sent < batch_size && retries < 10) {
      int msgs_to_send = batch_size - batch_sent;
      int msgs_sent = sendmmsg(sockfd, &msgs[batch_sent], msgs_to_send, 0);

      if (msgs_sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // Wait for buffer space (poll)
          struct pollfd pfd;
          pfd.fd = sockfd;
          pfd.events = POLLOUT;

          int poll_result = poll(&pfd, 1, 100); // 100ms timeout

          if (poll_result < 0) {
            logs::log(logs::warning, "poll() failed in send_batch: {}", errno);
            return false;
          } else if (poll_result == 0) {
            // Timeout - retry
            retries++;
            continue;
          }
          // Socket is ready, try again
          continue;
        } else if (errno == EINTR) {
          // Interrupted, retry
          continue;
        } else {
          logs::log(logs::warning, "sendmmsg() failed: {} (errno={})", std::strerror(errno), errno);
          return false;
        }
      }

      batch_sent += msgs_sent;
      blocks_sent += msgs_sent;
      remaining_blocks -= msgs_sent;
      retries = 0; // Reset retry counter on success
    }

    if (retries >= 10) {
      logs::log(logs::warning, "sendmmsg() exceeded retry limit");
      return false;
    }
  }

  return blocks_sent >= send_info.block_count;
}

void configure_socket_for_streaming(boost::asio::ip::udp::socket &socket, bool is_video) {
  int native_sock = socket.native_handle();

  // Set non-blocking mode
  int flags = fcntl(native_sock, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(native_sock, F_SETFL, flags | O_NONBLOCK);
  }

  // Increase send buffer size to 2MB
  int sndbuf_size = 2 * 1024 * 1024;
  if (setsockopt(native_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size)) < 0) {
    logs::log(logs::warning, "Failed to set SO_SNDBUF");
  }

  // Set socket priority (SO_PRIORITY)
  int priority = is_video ? 5 : 6; // Audio gets higher priority
  if (setsockopt(native_sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0) {
    logs::log(logs::warning, "Failed to set SO_PRIORITY");
  }

  logs::log(logs::debug, "Socket configured for streaming (sndbuf=2MB, priority={})", priority);
}

void enable_socket_qos(int native_socket, bool is_video) {
  // DSCP tagging
  int dscp = is_video ? 40 : 48; // Video: CS5 (40), Audio: CS6 (48)
  dscp <<= 2;                    // Shift to TOS field position

  if (setsockopt(native_socket, IPPROTO_IP, IP_TOS, &dscp, sizeof(dscp)) < 0) {
    logs::log(logs::debug, "Failed to set IP_TOS/DSCP");
  }
}

} // namespace wolf::platform
