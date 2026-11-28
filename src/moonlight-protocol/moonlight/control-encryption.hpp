#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace moonlight::control {

inline constexpr int CONTROL_V2_FLAG = 0x01;

enum class ControlMode {
  pending,
  legacy,
  v2
};
enum class ControlOrigin : std::uint8_t {
  client = 'C',
  host = 'H'
};

struct ControlNonce {
  std::array<char, 16> bytes{};
  int length = 16;

  std::string_view view() const {
    return {bytes.data(), static_cast<std::size_t>(length)};
  }
};

inline ControlNonce control_nonce(std::uint32_t sequence, ControlMode mode, ControlOrigin origin) {
  ControlNonce nonce;
  nonce.bytes[0] = static_cast<char>(sequence & 0xff);
  if (mode == ControlMode::v2) {
    nonce.length = 12;
    nonce.bytes[1] = static_cast<char>((sequence >> 8) & 0xff);
    nonce.bytes[2] = static_cast<char>((sequence >> 16) & 0xff);
    nonce.bytes[3] = static_cast<char>((sequence >> 24) & 0xff);
    nonce.bytes[10] = static_cast<char>(origin);
    nonce.bytes[11] = 'C';
  }
  return nonce;
}

// Shared by the RTSP and ENet snapshots of one session key. Only the ENet
// loop assigns sequence numbers. Once selected, the mode cannot be changed.
class ControlEncryption {
public:
  explicit ControlEncryption(bool allow_v2 = true) : allow_v2_(allow_v2) {}

  bool negotiate(int flags) {
    const auto requested = (flags & CONTROL_V2_FLAG) ? ControlMode::v2 : ControlMode::legacy;
    if (requested == ControlMode::v2 && !allow_v2_) {
      return false;
    }
    auto expected = ControlMode::pending;
    return mode_.compare_exchange_strong(expected, requested) || expected == requested;
  }

  ControlMode mode() const {
    return mode_.load();
  }

  // ENet may retransmit a handshake, but a new V2 peer can restart its
  // sequence at zero only after another launch or resume supplies a fresh key.
  bool claim_peer() {
    switch (mode()) {
    case ControlMode::pending:
      return false;
    case ControlMode::legacy:
      return true;
    case ControlMode::v2:
      if (v2_peer_claimed_) {
        return false;
      }
      v2_peer_claimed_ = true;
      return true;
    }
    return false;
  }

  std::optional<std::uint32_t> next_host_sequence() {
    const auto selected_mode = mode();
    if (selected_mode == ControlMode::pending) {
      return std::nullopt;
    }
    if (selected_mode == ControlMode::legacy) {
      return 0;
    }
    if (next_sequence_ > std::numeric_limits<std::uint32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(next_sequence_++);
  }

private:
  const bool allow_v2_;
  std::atomic<ControlMode> mode_{ControlMode::pending};
  bool v2_peer_claimed_ = false;
  std::uint64_t next_sequence_ = 0;
};

} // namespace moonlight::control
