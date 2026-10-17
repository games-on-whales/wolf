#pragma once

#include <boost/endian/conversion.hpp>
#include <catch2/catch_test_macros.hpp>
#include <crypto/crypto.hpp>
#include <cstring>
#include <string>

/*
 * The "client" helpers below are written from moonlight-common-c (RtspConnection.c, VideoStream.c,
 * ControlStream.c), using the one-shot crypto:: functions, so that they check the wire format independently of
 * the server code under test.
 */

inline const std::string KEY = crypto::hex_to_str("9d804e47a6aa6624b7d4b502b32cc522", true);

inline std::string le32(std::uint32_t v) {
  v = boost::endian::native_to_little(v);
  return {(char *)&v, sizeof(v)};
}

inline std::string be32(std::uint32_t v) {
  v = boost::endian::native_to_big(v);
  return {(char *)&v, sizeof(v)};
}

/** moonlight-common-c's 12-byte IVs: little-endian sequence number in the first 4 bytes, tags at 10 and 11 */
inline std::string client_iv(std::uint32_t seq, char sender, char stream) {
  std::string iv(12, '\0');
  auto le = le32(seq);
  std::copy(le.begin(), le.end(), iv.begin());
  iv[10] = sender;
  iv[11] = stream;
  return iv;
}

/** sealRtspMessage() */
inline std::string client_seal_rtsp(std::uint32_t seq, std::string_view plaintext, const std::string &key = KEY) {
  auto [ciphertext, tag] = crypto::aes_encrypt_gcm(plaintext, key, client_iv(seq, 'C', 'R'), 12);
  return be32(0x80000000 | (std::uint32_t)plaintext.size()) + be32(seq) + tag + ciphertext;
}

/** unsealRtspMessage() */
inline std::string client_open_rtsp(std::string_view message) {
  REQUIRE(message.size() > 24);
  std::uint32_t type_and_length, seq;
  std::memcpy(&type_and_length, message.data(), 4);
  std::memcpy(&seq, message.data() + 4, 4);
  REQUIRE((boost::endian::big_to_native(type_and_length) & 0x80000000));
  REQUIRE((boost::endian::big_to_native(type_and_length) & 0x7FFFFFFF) == message.size() - 24);
  return crypto::aes_decrypt_gcm(message.substr(24),
                                 KEY,
                                 message.substr(8, 16),
                                 client_iv(boost::endian::big_to_native(seq), 'H', 'R'),
                                 12);
}
