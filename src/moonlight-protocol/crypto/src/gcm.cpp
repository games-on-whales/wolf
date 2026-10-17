#include <crypto/crypto.hpp>
#include <openssl/evp.h>
#include <stdexcept>

namespace crypto {

aes_gcm::aes_gcm(std::string_view key)
    : enc_(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free), dec_(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free) {
  if (key.size() != 16) {
    throw std::invalid_argument("AES-128-GCM needs a 16 byte key");
  }
  if (!enc_ || !dec_ || EVP_EncryptInit_ex(enc_.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(enc_.get(), EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1 ||
      EVP_EncryptInit_ex(enc_.get(), nullptr, nullptr, (const std::uint8_t *)key.data(), nullptr) != 1 ||
      EVP_DecryptInit_ex(dec_.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(dec_.get(), EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1 ||
      EVP_DecryptInit_ex(dec_.get(), nullptr, nullptr, (const std::uint8_t *)key.data(), nullptr) != 1) {
    throw std::runtime_error("AES-GCM context setup failed");
  }
}

void aes_gcm::seal(std::string_view iv, std::string_view plaintext, std::uint8_t *out, std::uint8_t *tag) {
  int len = 0;
  if (iv.size() != IV_SIZE ||
      EVP_EncryptInit_ex(enc_.get(), nullptr, nullptr, nullptr, (const std::uint8_t *)iv.data()) != 1 ||
      EVP_EncryptUpdate(enc_.get(), out, &len, (const std::uint8_t *)plaintext.data(), (int)plaintext.size()) != 1 ||
      EVP_EncryptFinal_ex(enc_.get(), out + len, &len) != 1 ||
      EVP_CIPHER_CTX_ctrl(enc_.get(), EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag) != 1) {
    throw std::runtime_error("AES-GCM seal failed");
  }
}

bool aes_gcm::open(std::string_view iv, std::string_view ciphertext, std::string_view tag, std::uint8_t *out) {
  int len = 0;
  if (iv.size() != IV_SIZE || tag.size() != TAG_SIZE ||
      EVP_DecryptInit_ex(dec_.get(), nullptr, nullptr, nullptr, (const std::uint8_t *)iv.data()) != 1 ||
      EVP_DecryptUpdate(dec_.get(), out, &len, (const std::uint8_t *)ciphertext.data(), (int)ciphertext.size()) != 1 ||
      EVP_CIPHER_CTX_ctrl(dec_.get(), EVP_CTRL_GCM_SET_TAG, TAG_SIZE, (void *)tag.data()) != 1) {
    return false;
  }
  // Final checks the tag: a forged or corrupted message fails here
  return EVP_DecryptFinal_ex(dec_.get(), out + len, &len) == 1;
}

} // namespace crypto
