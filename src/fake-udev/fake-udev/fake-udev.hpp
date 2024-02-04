#pragma once
#include "MurmurHash2.h"
#include <algorithm>
#include <iostream>
#include <linux/netlink.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

struct netlink_connection {
  int fd = -1;
  sockaddr_nl sa = {};
};

static bool connect(netlink_connection &conn, int domain, int type, int protocol, unsigned int groups) {
  int sock = socket(domain, type, protocol);
  if (sock < 0) {
    std::cout << "Could not connect to netlink socket:" << strerror(errno) << std::endl;
    return false;
  }
  conn.fd = sock;

  conn.sa.nl_family = domain;
  conn.sa.nl_groups = groups;
  int rc = bind(conn.fd, (struct sockaddr *)&conn.sa, sizeof(conn.sa));
  if (rc < 0) {
    std::cout << "Could not bind to netlink socket:" << strerror(errno) << std::endl;
    return false;
  }
  return true;
}

static bool send_msgs(netlink_connection &conn, const std::vector<std::string /* raw payload */> &msgs) {
  iovec iov[msgs.size()];
  for (int i = 0; i < msgs.size(); ++i) {
    iov[i] = iovec{.iov_base = (char *)msgs[i].data(), .iov_len = msgs[i].size()};
  }

  msghdr msg = {
      .msg_name = &conn.sa,
      .msg_namelen = sizeof conn.sa,
      .msg_iov = iov,
      .msg_iovlen = msgs.size(),
  };
  int rc = sendmsg(conn.fd, &msg, 0);
  if (rc <= 0) {
    std::cout << "Could not send message:" << strerror(errno) << std::endl;
    return false;
  } else {
    return true;
  }
}

static void cleanup(netlink_connection &conn) {
  if (conn.fd >= 0) {
    close(conn.fd);
  }
}

/**
 * Adapted from https://stackoverflow.com/a/34571089
 */
static std::string base64_decode(const std::string &in) {
  std::string out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++)
    T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (T[c] == -1)
      break;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

/*
 * Taken from:
 * https://github.com/systemd/systemd/blob/61afc53924dd3263e7b76b1323a5fe61d589ffd2/src/libsystemd/sd-device/device-monitor.c#L67-L86
 * */
typedef struct monitor_netlink_header {
  /* "libudev" prefix to distinguish libudev and kernel messages */
  char prefix[8] = "libudev";
  /* Magic to protect against daemon <-> Library message format mismatch
   * Used in the kernel from socket filter rules; needs to be stored in network order */
  unsigned magic = 0xfeedcafe;
  /* Total length of header structure known to the sender */
  unsigned header_size;
  /* Properties string buffer */
  unsigned properties_off;
  unsigned properties_len;
  /* Hashes of primary device properties strings, to let libudev subscribers
   * use in-kernel socket filters; values need to be stored in network order */
  unsigned filter_subsystem_hash;
  unsigned filter_devtype_hash;
  unsigned filter_tag_bloom_hi;
  unsigned filter_tag_bloom_lo;
} monitor_netlink_header;

constexpr unsigned UDEV_MONITOR_MAGIC = 0xfeedcafe;

static uint32_t string_hash32(const std::string &str) {
  return MurmurHash2(str.c_str(), str.length(), 0);
}

static std::string
make_udev_header(const std::string &full_opts, const std::string &subsystem, const std::string &devtype) {
  monitor_netlink_header header{
      .magic = htobe32(UDEV_MONITOR_MAGIC),
      .header_size = sizeof header,
      .properties_off = sizeof header,
      .properties_len = static_cast<unsigned int>(full_opts.size()),
      .filter_tag_bloom_hi = 0, // TODO: ?
      .filter_tag_bloom_lo = 0, // TODO: ?
  };
  if (!subsystem.empty()) {
    header.filter_subsystem_hash = htobe32(string_hash32(subsystem));
  }
  if (!devtype.empty()) {
    header.filter_devtype_hash = htobe32(string_hash32(devtype));
  }

  return {(char *)&header, sizeof header};
}

/**
 * Adapted from https://stackoverflow.com/questions/865668/parsing-command-line-arguments-in-c
 */
class InputParser {
public:
  InputParser(int &argc, char **argv) {
    for (int i = 1; i < argc; ++i)
      this->tokens.push_back(std::string(argv[i]));
  }
  /// @author iain
  const std::string &getCmdOption(const std::string &option, const std::string &default_str = "") const {
    std::vector<std::string>::const_iterator itr;
    itr = std::find(this->tokens.begin(), this->tokens.end(), option);
    if (itr != this->tokens.end() && ++itr != this->tokens.end()) {
      return *itr;
    }
    return default_str;
  }

  const int getCmdOption(const std::string &option, int default_val) {
    auto val = getCmdOption(option);
    if (val.empty()) {
      return default_val;
    } else {
      return std::stoi(val);
    }
  }

  /// @author iain
  bool cmdOptionExists(const std::string &option) const {
    return std::find(this->tokens.begin(), this->tokens.end(), option) != this->tokens.end();
  }

private:
  std::vector<std::string> tokens;
};