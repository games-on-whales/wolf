#pragma once
#include <array>
#include <boost/endian/conversion.hpp>
#include <crypto/crypto.hpp>

namespace moonlight::control {

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

/**
 * Events received in the ControlSession will be fired up in the event_bus
 */
struct ControlEvent {
  // A unique ID that identifies this session
  std::size_t session_id;

  PACKET_TYPE type;
  std::string_view raw_packet;
};

struct PauseStreamEvent {
  std::size_t session_id;
};

struct ResumeStreamEvent {
  std::size_t session_id;
};

struct StopStreamEvent {
  std::size_t session_id;
};

static constexpr int GCM_TAG_SIZE = 16;

struct control_encrypted_t {
  std::uint16_t header_type; // Always 0x0001 (see PACKET_TYPE ENCRYPTED)
  std::uint16_t length;      // sizeof(seq) + 16 byte tag + sizeof(encrypted_msg)

  std::uint32_t seq;         // Monotonically increasing sequence number (used as IV for AES-GCM)

  /**
   * First 16 bytes are the AES GCM TAG
   */
  char gcm_tag[GCM_TAG_SIZE];

  /**
   * Rest of the bytes are the encrypted message
   */
  char payload[];

  /**
   * Helper function to get the payload as a string with the right size
   */
  [[nodiscard]] std::string_view encrypted_msg() const {
    auto len = boost::endian::little_to_native(this->length);
    return {payload, static_cast<size_t>(len - GCM_TAG_SIZE - sizeof(seq))};
  }
};

/**
 * Given a received packet will decrypt the payload inside it.
 * This includes checking that the AES GCM TAG is valid and not tampered
 */
static std::string decrypt_packet(const control_encrypted_t &packet_data, std::string_view gcm_key) {
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
static control_encrypted_t encrypt_packet(std::string_view gcm_key, std::uint32_t seq, std::string_view payload) {
  std::array<std::uint8_t, GCM_TAG_SIZE> iv_data = {0};
  iv_data[0] = boost::endian::native_to_little(seq);

  auto [encrypted_str, gcm_tag] = crypto::aes_encrypt_gcm(payload,
                                                          crypto::hex_to_str(gcm_key.data(), true),
                                                          {(char *)iv_data.data(), iv_data.size()},
                                                          GCM_TAG_SIZE);

  std::uint16_t size = sizeof(seq) + GCM_TAG_SIZE + encrypted_str.length();
  control_encrypted_t encrypted_pkt = {.header_type = boost::endian::native_to_little((std::uint16_t)ENCRYPTED),
                                       .length = boost::endian::native_to_little(size),
                                       .seq = boost::endian::native_to_little(seq)};

  std::copy(gcm_tag.begin(), gcm_tag.end(), encrypted_pkt.gcm_tag);
  std::copy(encrypted_str.begin(), encrypted_str.end(), encrypted_pkt.payload);

  return encrypted_pkt;
}

static PACKET_TYPE get_type(std::string_view packet_payload) {
  auto type = *(std::uint16_t *)packet_payload.data();
  auto type_little = boost::endian::little_to_native(type);
  return (PACKET_TYPE)type_little;
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
  }
  return "Unrecognised";
}

} // namespace moonlight::control