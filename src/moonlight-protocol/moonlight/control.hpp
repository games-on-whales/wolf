#pragma once
#include <array>
#include <boost/endian/conversion.hpp>
#include <core/api.hpp>
#include <crypto/crypto.hpp>
#include <cstdint>
#include <memory>

namespace moonlight::control {

using namespace wolf::core::api;

static constexpr int GCM_TAG_SIZE = 16;
static constexpr int MAX_PAYLOAD_SIZE = 128;
static constexpr std::uint32_t TERMINATE_REASON_GRACEFULL = boost::endian::native_to_big(0x80030023);

struct ControlPacket {
  PACKET_TYPE type;
  std::uint16_t length; // The length of the REST of the packet, EXCLUDING size of type and length
};

struct ControlTerminatePacket {
  ControlPacket header = {.type = TERMINATION, .length = sizeof(std::uint32_t)};
  std::uint32_t reason = TERMINATE_REASON_GRACEFULL;
};

struct ControlEncryptedPacket {
  ControlPacket header; // Always 0x0001 (see PACKET_TYPE ENCRYPTED)
  std::uint32_t seq;    // Monotonically increasing sequence number (used as IV for AES-GCM)

  /**
   * First 16 bytes are the AES GCM TAG
   */
  char gcm_tag[GCM_TAG_SIZE];

  /**
   * Rest of the bytes are the encrypted message
   */
  char payload[MAX_PAYLOAD_SIZE]; // TODO: this should be a char* with a variable size based on header

  /**
   * Helper function to get the payload as a string with the right size
   */
  [[nodiscard]] std::string_view encrypted_msg() const {
    auto len = boost::endian::little_to_native(this->header.length);
    return {payload, static_cast<size_t>(len - GCM_TAG_SIZE - sizeof(seq))};
  }

  [[nodiscard]] size_t full_size() const {
    return boost::endian::little_to_native(this->header.length) + sizeof(ControlPacket);
  }
};

/**
 * Given a received packet will decrypt the payload inside it.
 * This includes checking that the AES GCM TAG is valid and not tampered
 */
static std::string decrypt_packet(const ControlEncryptedPacket &packet_data, std::string_view gcm_key) {
  std::array<std::uint8_t, GCM_TAG_SIZE> iv_data = {0};
  iv_data[0] = boost::endian::little_to_native(packet_data.seq);

  return crypto::aes_decrypt_gcm(packet_data.encrypted_msg(),
                                 crypto::hex_to_str(gcm_key.data(), true),
                                 packet_data.gcm_tag,
                                 {(char *)iv_data.data(), iv_data.size()},
                                 GCM_TAG_SIZE);
}

/**
 * Turns a payload into a properly formatted control encrypted packet
 */
static std::unique_ptr<ControlEncryptedPacket>
encrypt_packet(std::string_view gcm_key, std::uint32_t seq, std::string_view payload) {
  std::array<std::uint8_t, GCM_TAG_SIZE> iv_data = {0};
  iv_data[0] = boost::endian::native_to_little(seq);

  auto [encrypted_str, gcm_tag] = crypto::aes_encrypt_gcm(payload,
                                                          crypto::hex_to_str(gcm_key.data(), true),
                                                          {(char *)iv_data.data(), iv_data.size()},
                                                          GCM_TAG_SIZE);

  std::uint16_t size = sizeof(seq) + GCM_TAG_SIZE + encrypted_str.length();
  ControlEncryptedPacket encrypted_pkt = {
      .header = {.type = ENCRYPTED, .length = boost::endian::native_to_little(size)},
      .seq = boost::endian::native_to_little(seq)};

  std::copy(gcm_tag.begin(), gcm_tag.end(), encrypted_pkt.gcm_tag);
  std::copy(encrypted_str.begin(), encrypted_str.end(), encrypted_pkt.payload);

  return std::make_unique<ControlEncryptedPacket>(encrypted_pkt);
}

static constexpr const char *packet_type_to_str(PACKET_TYPE p) noexcept {
  switch (p) {
  case START_A:
    return "START_A";
  case START_B:
    return "START_B";
  case INVALIDATE_REF_FRAMES:
    return "INVALIDATE_REF_FRAMES";
  case LOSS_STATS:
    return "LOSS_STATS";
  case FRAME_STATS:
    return "FRAME_STATS";
  case INPUT_DATA:
    return "INPUT_DATA";
  case RUMBLE_DATA:
    return "RUMBLE_DATA";
  case TERMINATION:
    return "TERMINATION";
  case PERIODIC_PING:
    return "PERIODIC_PING";
  case IDR_FRAME:
    return "IDR_FRAME";
  case ENCRYPTED:
    return "ENCRYPTED";
  case HDR_MODE:
    return "HDR_MODE";
  case RUMBLE_TRIGGERS:
    return "RUMBLE_TRIGGERS";
  case MOTION_EVENT:
    return "MOTION_EVENT";
  case RGB_LED:
    return "RGB_LED";
  }
  return "Unrecognised";
}

} // namespace moonlight::control