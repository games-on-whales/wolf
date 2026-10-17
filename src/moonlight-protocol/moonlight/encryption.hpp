#pragma once

#include <array>
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <crypto/crypto.hpp>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

/**
 * Stream encryption as negotiated by Sunshine-compatible Moonlight clients (moonlight-common-c).
 *
 * The client generates a fresh AES-128 key per launch (`rikey`) and sends it to us over the paired, mutually
 * authenticated HTTPS `/launch` (or `/resume`) request. Everything below is keyed with it:
 *  - RTSP: AES-GCM over each whole message when the launch response advertises an `rtspenc://` URL
 *  - Control stream V2: AES-GCM with a 12-byte IV built from a 32-bit sequence number
 *  - Video: AES-GCM over every RTP packet (data and FEC shards), prefixed by an `ENC_VIDEO_HEADER`
 *  - Audio: the pre-existing AES-CBC scheme (`x-nv-general.featureFlags` 0x20)
 *
 * Every IV ends with two bytes naming the sender (H = host, C = client) and the stream (R = RTSP, C = control,
 * V = video), so the same key never sees the same IV on two different streams or directions.
 */
namespace moonlight::encryption {

// Values of the x-ss-general.encryption{Supported,Requested,Enabled} SDP attributes
constexpr std::uint32_t CONTROL_V2 = 0x01;
constexpr std::uint32_t VIDEO = 0x02;
constexpr std::uint32_t AUDIO = 0x04;
constexpr std::uint32_t ALL = CONTROL_V2 | VIDEO | AUDIO;

/**
 * Per-network policy, mirroring Sunshine's lan_encryption_mode/wan_encryption_mode
 */
enum class Mode {
  DISABLED = 0, // never encrypt video or RTSP
  OPTIONAL = 1, // encrypt everything the client supports
  REQUIRED = 2, // refuse clients that can't encrypt RTSP, control, video and audio
};

/**
 * Parses 0/1/2 or disabled/optional/required (case insensitive); anything else returns fallback.
 */
inline Mode parse_mode(std::string_view value, Mode fallback) {
  std::string v(value);
  for (auto &c : v) {
    c = (char)std::tolower((unsigned char)c);
  }
  if (v == "0" || v == "disabled") {
    return Mode::DISABLED;
  }
  if (v == "1" || v == "optional") {
    return Mode::OPTIONAL;
  }
  if (v == "2" || v == "required") {
    return Mode::REQUIRED;
  }
  return fallback;
}

/**
 * A 12-byte AES-GCM IV: a little-endian counter in the low bytes, then the sender and stream tags.
 */
inline std::array<char, crypto::aes_gcm::IV_SIZE> make_iv(std::uint32_t seq, char sender, char stream) {
  std::array<char, crypto::aes_gcm::IV_SIZE> iv = {0};
  auto le = boost::endian::native_to_little(seq);
  std::memcpy(iv.data(), &le, sizeof(le));
  iv[10] = sender;
  iv[11] = stream;
  return iv;
}

/* ---------------------------------------------------------------- RTSP */

constexpr std::uint32_t ENCRYPTED_RTSP_BIT = 0x80000000;
constexpr std::size_t RTSP_HEADER_SIZE = 4 /* typeAndLength */ + 4 /* sequenceNumber */ + crypto::aes_gcm::TAG_SIZE;
/** Bounds what we'll buffer for one encrypted RTSP request */
constexpr std::size_t MAX_RTSP_MESSAGE_SIZE = 64 * 1024;

/**
 * Plaintext RTSP always starts with an ASCII method; an encrypted message starts with the encrypted bit.
 */
inline bool is_encrypted_rtsp(std::string_view data) {
  return !data.empty() && (static_cast<std::uint8_t>(data[0]) & 0x80);
}

/**
 * @return the full size (header included) of the encrypted message starting at data,
 *         or nullopt while fewer than 4 bytes have been read
 */
inline std::optional<std::size_t> encrypted_rtsp_size(std::string_view data) {
  if (data.size() < 4) {
    return std::nullopt;
  }
  std::uint32_t type_and_length;
  std::memcpy(&type_and_length, data.data(), sizeof(type_and_length));
  type_and_length = boost::endian::big_to_native(type_and_length);
  return RTSP_HEADER_SIZE + (type_and_length & ~ENCRYPTED_RTSP_BIT);
}

struct OpenedRtsp {
  std::uint32_t seq;
  std::string plaintext;
};

/**
 * Authenticates and decrypts a complete client RTSP message.
 * @return nullopt when it isn't encrypted, is truncated or padded, or wasn't sealed with this key
 */
inline std::optional<OpenedRtsp> open_rtsp(crypto::aes_gcm &gcm, std::string_view message) {
  if (!is_encrypted_rtsp(message) || message.size() <= RTSP_HEADER_SIZE ||
      encrypted_rtsp_size(message) != message.size()) {
    return std::nullopt;
  }
  std::uint32_t seq;
  std::memcpy(&seq, message.data() + 4, sizeof(seq));
  seq = boost::endian::big_to_native(seq);
  auto iv = make_iv(seq, 'C', 'R');
  auto tag = message.substr(8, crypto::aes_gcm::TAG_SIZE);
  auto ciphertext = message.substr(RTSP_HEADER_SIZE);
  std::string plaintext(ciphertext.size(), '\0');
  if (!gcm.open({iv.data(), iv.size()}, ciphertext, tag, (std::uint8_t *)plaintext.data())) {
    return std::nullopt;
  }
  return OpenedRtsp{.seq = seq, .plaintext = std::move(plaintext)};
}

/**
 * Seals a host RTSP message. seq must be unique for this key: we reply with the sequence number of the
 * (strictly increasing, replay checked) request we're answering.
 */
inline std::string seal_rtsp(crypto::aes_gcm &gcm, std::uint32_t seq, std::string_view plaintext) {
  std::string out(RTSP_HEADER_SIZE + plaintext.size(), '\0');
  auto type_and_length = boost::endian::native_to_big(ENCRYPTED_RTSP_BIT | (std::uint32_t)plaintext.size());
  auto be_seq = boost::endian::native_to_big(seq);
  std::memcpy(out.data(), &type_and_length, sizeof(type_and_length));
  std::memcpy(out.data() + 4, &be_seq, sizeof(be_seq));
  auto iv = make_iv(seq, 'H', 'R');
  gcm.seal({iv.data(), iv.size()},
           plaintext,
           (std::uint8_t *)out.data() + RTSP_HEADER_SIZE,
           (std::uint8_t *)out.data() + 8);
  return out;
}

/* --------------------------------------------------------------- Video */

/**
 * Mirrors moonlight-common-c's ENC_VIDEO_HEADER, which precedes every encrypted video packet:
 * the 12-byte IV, the frame number (a cleartext hint for discarding stale shards) and the GCM tag.
 */
#pragma pack(push, 1)
struct EncVideoHeader {
  char iv[crypto::aes_gcm::IV_SIZE];
  boost::endian::little_uint32_at frame_number;
  char tag[crypto::aes_gcm::TAG_SIZE];
};
#pragma pack(pop)
static_assert(sizeof(EncVideoHeader) == 32);

/**
 * The video IV: a 48-bit packet counter, a 32-bit epoch that changes every time a pipeline (re)starts for the
 * same key, and the H/V tags. A pipeline would need 2^48 packets to wrap.
 */
inline std::array<char, crypto::aes_gcm::IV_SIZE> make_video_iv(std::uint64_t counter, std::uint32_t epoch) {
  std::array<char, crypto::aes_gcm::IV_SIZE> iv = {0};
  for (int i = 0; i < 6; i++) {
    iv[i] = (char)((counter >> (8 * i)) & 0xFF);
  }
  auto le_epoch = boost::endian::native_to_little(epoch);
  std::memcpy(iv.data() + 6, &le_epoch, sizeof(le_epoch));
  iv[10] = 'H';
  iv[11] = 'V';
  return iv;
}

/**
 * Seals one RTP video packet into out, which must hold sizeof(EncVideoHeader) + packet.size() bytes.
 */
inline void seal_video(crypto::aes_gcm &gcm,
                       std::uint64_t counter,
                       std::uint32_t epoch,
                       std::uint32_t frame_number,
                       std::string_view packet,
                       std::uint8_t *out) {
  auto header = reinterpret_cast<EncVideoHeader *>(out);
  auto iv = make_video_iv(counter, epoch);
  std::memcpy(header->iv, iv.data(), iv.size());
  header->frame_number = frame_number;
  gcm.seal({iv.data(), iv.size()}, packet, out + sizeof(EncVideoHeader), (std::uint8_t *)header->tag);
}

/* -------------------------------------------------------- Replay guard */

/**
 * A sliding window over 32-bit sequence numbers (RFC 4303 style): accepts each number at most once and rejects
 * anything older than the window. Control packets travel on several ENet channels, so they may arrive slightly out
 * of order; RTSP requests are sequential, so use a window of 1 there to require strictly increasing numbers.
 */
template <std::size_t WINDOW> class ReplayWindow {
  static_assert(WINDOW >= 1 && WINDOW <= 64);

public:
  /** @return true (and records it) when seq hasn't been seen and is recent enough */
  bool accept(std::uint32_t seq) {
    if (!started_) {
      started_ = true;
      highest_ = seq;
      seen_ = 1;
      return true;
    }
    if (seq > highest_) {
      auto shift = seq - highest_;
      seen_ = shift >= 64 ? 0 : seen_ << shift;
      seen_ |= 1;
      highest_ = seq;
      return true;
    }
    auto age = highest_ - seq;
    if (age >= WINDOW || (seen_ >> age) & 1) {
      return false;
    }
    seen_ |= std::uint64_t(1) << age;
    return true;
  }

private:
  bool started_ = false;
  std::uint32_t highest_ = 0;
  std::uint64_t seen_ = 0;
};

} // namespace moonlight::encryption
