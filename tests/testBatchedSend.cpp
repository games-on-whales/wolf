#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

using Catch::Matchers::Equals;

#include <core/batched_send.hpp>
#include <fcntl.h>
#include <gst-video-context.hpp>
#include <streaming/streaming.hpp>
#include <sys/socket.h>
#include <unistd.h>

TEST_CASE("Batched send") {
  int sv[2]; // sv[0] = sender, sv[1] = receiver

  REQUIRE(socketpair(AF_UNIX,    // Unix domain — stays in kernel, no loopback
                     SOCK_DGRAM, // preserves message boundaries (like UDP)
                     0,          // protocol (0 = default for domain)
                     sv) == 0);

  // Set non-blocking so you know when messages are exhausted
  fcntl(sv[1], F_SETFL, O_NONBLOCK);

  std::vector<std::string> messages = {"test", "test2"};

  wolf::platform::batched_send_info_t send_info{.block_count = messages.size(), .native_socket = sv[0]};
  for (auto &msg : messages) {
    send_info.payload_buffers.emplace_back(msg.data(), msg.size());
  }

  // Send the batch
  REQUIRE(wolf::platform::send_batch(send_info));

  // Read back from the socket pair
  std::vector<std::string> received;
  char buf[4096];
  ssize_t n;
  while ((n = recv(sv[1], buf, sizeof(buf), 0)) > 0) {
    received.emplace_back(buf, n);
  }
  REQUIRE(errno == EAGAIN); // errno == EAGAIN means no more messages — not an error
  REQUIRE_THAT(received, Equals(messages));

  close(sv[0]);
  close(sv[0]);
}

/**
 * The video send pacing rate must not cap the frame rate of high-bitrate streams: it has to stay
 * well above the stream's own wire rate while keeping the historical 1 Gbit/s floor for ordinary
 * bitrates. See streaming::video_pacing_packets_per_ms.
 */
TEST_CASE("Video pacing rate", "[Pacing]") {
  // 80% of 1 Gbit/s at 1392 byte packets: ordinary bitrates keep the historical pacing rate.
  REQUIRE(streaming::video_pacing_packets_per_ms(20000, 20, 1392) == 71);

  // What Wolf derives for 5120x1440@120 (764 Mbit/s on the wire) and @240 (1080 Mbit/s). 3x the
  // wire rate is what clears a frame in about a third of its frame interval.
  REQUIRE(streaming::video_pacing_packets_per_ms(610508, 20, 1392) == 198);
  REQUIRE(streaming::video_pacing_packets_per_ms(863308, 20, 1392) == 280);
}