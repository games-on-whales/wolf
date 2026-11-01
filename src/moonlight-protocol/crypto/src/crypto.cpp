#include "aes.hpp"
#include "sign.hpp"
#include <algorithm>
#include <iomanip>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <sstream>
#include <stdexcept>

namespace crypto {

std::string sha256(std::string_view str) {
  unsigned char md_value[EVP_MAX_MD_SIZE];
  unsigned int md_len;
  auto mdctx = EVP_MD_CTX_new();

  EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(mdctx, str.data(), str.size());
  EVP_DigestFinal_ex(mdctx, md_value, &md_len);
  EVP_MD_CTX_free(mdctx);

  std::stringstream ss;
  for (int i = 0; i < md_len; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)md_value[i];
  }
  return ss.str();
}

std::string str_to_hex(std::string_view input) {
  static const char hex_digits[] = "0123456789ABCDEF";

  std::string output;
  output.reserve(input.length() * 2);
  for (unsigned char c : input) {
    output.push_back(hex_digits[c >> 4]);
    output.push_back(hex_digits[c & 15]);
  }
  return output;
}

std::string hex_to_str(std::string_view hex, bool reverse = true) {
  std::string buf;

  static char constexpr shift_bit = 'a' - 'A';
  auto is_convertable = [](char ch) -> bool {
    if (isdigit(ch)) {
      return true;
    }
    ch |= shift_bit;
    if ('a' > ch || ch > 'z') {
      return false;
    }
    return true;
  };

  auto buf_size = std::count_if(std::begin(hex), std::end(hex), is_convertable) / 2;
  buf.resize(buf_size);

  const char *data = hex.data() + hex.size() - 1;

  auto convert = [](char ch) -> std::uint8_t {
    if (ch >= '0' && ch <= '9') {
      return (std::uint8_t)ch - '0';
    }
    return (std::uint8_t)(ch | (char)32) - 'a' + (char)10;
  };

  for (auto &el : buf) {
    while (!is_convertable(*data)) {
      --data;
    }
    std::uint8_t ch_r = convert(*data--);
    while (!is_convertable(*data)) {
      --data;
    }
    std::uint8_t ch_l = convert(*data--);
    el = (ch_l << 4) | ch_r;
  }

  if (reverse) {
    std::reverse(std::begin(buf), std::end(buf));
  }

  return buf;
}

std::string random(int length) {
  std::string rnd;
  rnd.resize(length);
  if (RAND_bytes((uint8_t *)rnd.data(), length) != 1)
    handle_openssl_error("RAND_bytes failed");
  return rnd;
}

std::string aes_encrypt_ecb(std::string_view msg, std::string_view enc_key, std::string_view iv, bool padding) {
  auto ctx = aes::init(EVP_aes_128_ecb(), enc_key, iv, true, padding);
  return aes::encrypt_symmetric(ctx.get(), msg);
}

std::string aes_decrypt_ecb(std::string_view msg, std::string_view enc_key, std::string_view iv, bool padding) {
  auto ctx = aes::init(EVP_aes_128_ecb(), enc_key, iv, false, padding);
  return aes::decrypt_symmetric(ctx.get(), msg);
}

std::string aes_encrypt_cbc(std::string_view msg, std::string_view enc_key, std::string_view iv, bool padding) {
  auto ctx = aes::init(EVP_aes_128_cbc(), enc_key, iv, true, padding);
  return aes::encrypt_symmetric(ctx.get(), msg);
}

std::string aes_decrypt_cbc(std::string_view msg, std::string_view enc_key, std::string_view iv, bool padding) {
  auto ctx = aes::init(EVP_aes_128_cbc(), enc_key, iv, false, padding);
  return aes::decrypt_symmetric(ctx.get(), msg);
}

std::pair<std::string, std::string> aes_encrypt_gcm(std::string_view msg,
                                                    std::string_view enc_key,
                                                    std::string_view iv = random(AES_BLOCK_SIZE),
                                                    int iv_size = -1,
                                                    bool padding = false) {
  auto ctx = aes::init(EVP_aes_128_gcm(), enc_key, iv, false, padding);

  if (iv_size != -1) {
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv_size, nullptr) != 1)
      handle_openssl_error("EVP_CTRL_GCM_SET_IVLEN failed");

    if (EVP_DecryptInit_ex(ctx.get(),
                           nullptr,
                           nullptr,
                           (const std::uint8_t *)enc_key.data(),
                           (const std::uint8_t *)iv.data()) != 1)
      handle_openssl_error("EVP_DecryptInit_ex (2) failed");

    EVP_CIPHER_CTX_set_padding(ctx.get(), padding);
  }

  return aes::encrypt_authenticated(ctx.get(), msg);
}

std::string aes_decrypt_gcm(std::string_view msg,
                            std::string_view enc_key,
                            std::string_view tag,
                            std::string_view iv = random(AES_BLOCK_SIZE),
                            int iv_size = -1,
                            bool padding = false) {
  auto ctx = aes::init(EVP_aes_128_gcm(), enc_key, iv, false, padding);

  if (iv_size != -1) {
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv_size, nullptr) != 1)
      handle_openssl_error("EVP_CTRL_GCM_SET_IVLEN failed");

    if (EVP_DecryptInit_ex(ctx.get(),
                           nullptr,
                           nullptr,
                           (const std::uint8_t *)enc_key.data(),
                           (const std::uint8_t *)iv.data()) != 1)
      handle_openssl_error("EVP_DecryptInit_ex (2) failed");

    EVP_CIPHER_CTX_set_padding(ctx.get(), padding);
  }

  return aes::decrypt_authenticated(ctx.get(), msg, tag);
}

std::string sign(std::string_view msg, std::string_view private_key) {
  auto p_key = signature::create_key(private_key, true);
  return signature::sign(msg, p_key.get(), EVP_sha256());
}

bool verify(std::string_view msg, std::string_view signature, std::string_view public_key) {
  auto p_key = signature::create_key(public_key, false);
  if (!p_key) {
    return false;
  }
  return signature::verify(msg, signature, p_key.get(), EVP_sha256());
}

} // namespace crypto