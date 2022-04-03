#include "aes.cpp"
#include <moonlight/crypto.hpp>

#include <openssl/pem.h>
#include <openssl/sha.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace crypto {

std::string sha256(const std::string &str) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, str.c_str(), str.size());
  SHA256_Final(hash, &sha256);
  std::stringstream ss;
  for (unsigned char i : hash) {
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)i;
  }
  return ss.str();
}

std::string pem(const X509 &x509) {
  X509 *cert_ptr = const_cast<X509 *>(&x509);
  BIO *bio_out = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(bio_out, cert_ptr);
  BUF_MEM *bio_buf;
  BIO_get_mem_ptr(bio_out, &bio_buf);
  std::string pem = std::string(bio_buf->data, bio_buf->length);
  BIO_free(bio_out);
  return pem;
}

std::string str_to_hex(const std::string &input) {
  static const char hex_digits[] = "0123456789ABCDEF";

  std::string output;
  output.reserve(input.length() * 2);
  for (unsigned char c : input) {
    output.push_back(hex_digits[c >> 4]);
    output.push_back(hex_digits[c & 15]);
  }
  return output;
}

/**
 * @brief Takes an HEX vector and returns a string representation of it.
 * Taken from Sunshine, TODO: refactor and better understanding of this
 */
std::string hex_to_str(const std::string &hex, bool reverse) {
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

std::string aes_encrypt_cbc(const std::string &msg, const std::string &enc_key, const std::string &iv, bool padding) {
  auto enc_key_uc = to_unsigned(enc_key);
  auto msg_uc = to_unsigned(msg);
  auto iv_uc = to_unsigned(iv);

  auto ctx = aes_init_enc(EVP_aes_128_ecb(), enc_key_uc.get(), iv_uc.get(), padding);
  return aes_encrypt(ctx.get(), msg_uc.get());
}

std::string aes_decrypt_cbc(const std::string &msg, const std::string &enc_key, const std::string &iv, bool padding) {
  auto enc_key_uc = to_unsigned(enc_key);
  auto msg_uc = to_unsigned(msg);
  auto iv_uc = to_unsigned(iv);

  auto ctx = aes_init_dec(EVP_aes_128_ecb(), enc_key_uc.get(), iv_uc.get(), padding);
  return aes_decrypt(ctx.get(), msg_uc.get());
}

using EVP_MD_CTX_ptr = std::unique_ptr<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;

std::string evp_sign(const unsigned char *msg, EVP_PKEY *key_data, const EVP_MD *digest_type) {
  EVP_MD_CTX_ptr ctx(EVP_MD_CTX_create(), ::EVP_MD_CTX_free);
  int msg_size = (int)strlen((char *)msg);

  if (EVP_DigestSignInit(ctx.get(), nullptr, digest_type, nullptr, key_data) != 1)
    handle_openssl_error("EVP_DigestSignInit failed");

  if (EVP_DigestSignUpdate(ctx.get(), msg, msg_size) != 1)
    handle_openssl_error("EVP_DigestSignUpdate failed");

  std::size_t digest_size = 256;
  unsigned char digest[digest_size];

  if (EVP_DigestSignFinal(ctx.get(), digest, &digest_size) != 1)
    handle_openssl_error("EVP_DigestSignFinal failed");

  return {reinterpret_cast<char *>(digest), digest_size};
}

bool evp_verify(const unsigned char *msg,
                const unsigned char *signature,
                EVP_PKEY *key_data,
                const EVP_MD *digest_type) {
  EVP_MD_CTX_ptr ctx(EVP_MD_CTX_create(), ::EVP_MD_CTX_free);
  int msg_size = (int)strlen((char *)msg);

  if (EVP_DigestVerifyInit(ctx.get(), nullptr, digest_type, nullptr, key_data) != 1)
    handle_openssl_error("EVP_DigestSignInit failed");

  if (EVP_DigestVerifyUpdate(ctx.get(), msg, msg_size) != 1)
    handle_openssl_error("EVP_DigestSignUpdate failed");

  std::size_t signature_size = strlen((char *)signature);
  return EVP_DigestVerifyFinal(ctx.get(), signature, signature_size) == 1;
}

using EVP_PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)>;

EVP_PKEY_ptr create_key(const unsigned char *k, bool is_private) {
  BIO *bio = BIO_new(BIO_s_mem());

  int k_size = (int)strlen((char *)k);
  if (BIO_write(bio, k, k_size) <= 0)
    handle_openssl_error("BIO_write failed");

  EVP_PKEY *p_key = nullptr;
  if (is_private)
    PEM_read_bio_PrivateKey(bio, &p_key, nullptr, nullptr);
  else
    PEM_read_bio_PUBKEY(bio, &p_key, nullptr, nullptr);
  if (p_key == nullptr)
    handle_openssl_error("PEM_read_bio_PrivateKey failed");

  BIO_free(bio);
  return {p_key, ::EVP_PKEY_free};
}

std::string sign(const std::string &msg, const std::string &private_key) {
  auto pkey_uc = to_unsigned(private_key);
  auto msg_uc = to_unsigned(msg);

  auto p_key = create_key(pkey_uc.get(), true);
  return evp_sign(msg_uc.get(), p_key.get(), EVP_sha256());
}

bool verify(const std::string &msg, const std::string &signature, const std::string &public_key) {
  auto pkey_uc = to_unsigned(public_key);
  auto msg_uc = to_unsigned(msg);
  auto sig_uc = to_unsigned(signature);

  auto p_key = create_key(pkey_uc.get(), false);
  return evp_verify(msg_uc.get(), sig_uc.get(), p_key.get(), EVP_sha256());
}

} // namespace crypto