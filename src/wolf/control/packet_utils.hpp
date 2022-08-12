#pragma once
#include <array>
#include <boost/endian/conversion.hpp>
#include <crypto/crypto.hpp>
#include <enet/enet.h>

namespace control {

enum PACKET_TYPE {
  START_A = 0x0305,
  START_B = 0x0307,
  INVALIDATE_REF_FRAMES = 0x0301,
  LOSS_STATS = 0x0201,
  FRAME_STATS = 0x0204,
  INPUT_DATA = 0x0206,
  RUMBLE_DATA = 0x010b,
  TERMINATION = 0x0100,
  PERIODIC_PING = 0x0200,
  IDR_FRAME = 0x0302,
  ENCRYPTED = 0x0001
};

static constexpr int GCM_TAG_SIZE = 16;

PACKET_TYPE get_type(const enet_uint8 *packet_data) {
  auto type = *(std::uint16_t *)packet_data;
  auto type_little = boost::endian::little_to_native(type);
  return (PACKET_TYPE)type_little;
}

typedef struct control_encrypted_t {
  std::uint16_t encryptedHeaderType; // Always 0x0001 (see PACKET_TYPE)
  std::uint16_t length;              // sizeof(seq) + 16 byte tag + encrypted_msg

  std::uint32_t seq; // Monotonically increasing sequence number (used as IV for AES-GCM)

  /**
   * First 16 bytes are the AES GCM TAG
   */
  std::string_view gcm_tag() {
    auto begin = (char *)(this + 1); // +1 here means + sizeof(control_encrypted_t)
    return {begin, GCM_TAG_SIZE};
  }

  /**
   * Rest of the bytes are the encrypted message
   */
  std::string_view encrypted_msg() {
    auto begin = (char *)(this + 1); // +1 here means + sizeof(control_encrypted_t)
    auto len = boost::endian::little_to_native(this->length);
    return {begin + GCM_TAG_SIZE, static_cast<size_t>(len - GCM_TAG_SIZE - sizeof(seq))};
  }
} * control_encrypted_p;

/**
 * Given a received packet will decrypt the message inside it.
 * This includes checking that the AES GCM TAG is valid and not tampered
 */
std::string decrypt_packet(const enet_uint8 *packet_data, std::string_view gcm_key) {
  auto header = (control_encrypted_p)(packet_data);

  constexpr int iv_size = 16;
  std::array<std::uint8_t, iv_size> iv = {0};
  iv[0] = (std::uint8_t)boost::endian::little_to_native(header->seq);
  std::string_view iv_str{(char *)iv.data(), iv_size};

  return crypto::aes_decrypt_gcm(header->encrypted_msg(), gcm_key, header->gcm_tag(), iv_str, iv_size);
}

constexpr const char *packet_type_to_str(PACKET_TYPE p) noexcept {
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
  }
  return "Unrecognised";
}

} // namespace control