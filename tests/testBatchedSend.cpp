#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

using Catch::Matchers::Equals;

#include <core/batched_send.hpp>
#include <fcntl.h>
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