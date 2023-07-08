#pragma once

#include <crypto/utils.hpp>
#include <cstdint>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace aes {
using CIPHER_CTX_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;
const int AES_GCM_TAG_SIZE = 16;

CIPHER_CTX_ptr init(const evp_cipher_st *chiper,
                    std::string_view key_data,
                    std::string_view iv,
                    bool is_encryption,
                    bool padding = true) {
  CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);

  if (is_encryption) {
    if (EVP_EncryptInit_ex(ctx.get(),
                           chiper,
                           nullptr,
                           (const std::uint8_t *)key_data.data(),
                           (const std::uint8_t *)iv.data()) != 1)
      handle_openssl_error("EVP_EncryptInit_ex failed");
  } else {
    if (EVP_DecryptInit_ex(ctx.get(),
                           chiper,
                           nullptr,
                           (const std::uint8_t *)key_data.data(),
                           (const std::uint8_t *)iv.data()) != 1)
      handle_openssl_error("EVP_DecryptInit_ex failed");
  }

  if (EVP_CIPHER_CTX_set_padding(ctx.get(), padding) != 1)
    handle_openssl_error("EVP_CIPHER_CTX_set_padding failed");

  return ctx;
}

std::string encrypt_symmetric(EVP_CIPHER_CTX *ctx, std::string_view plaintext) {
  /* max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE -1 bytes */
  auto len = (int)plaintext.size();
  int c_len = len + AES_BLOCK_SIZE;
  int f_len = 0;
  unsigned char ciphertext[c_len];

  /* allows reusing of 'ctx' for multiple encryption cycles */
  if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, nullptr) != 1)
    handle_openssl_error("EVP_EncryptInit_ex failed");

  if (EVP_EncryptUpdate(ctx, ciphertext, &c_len, (const std::uint8_t *)plaintext.data(), len) != 1)
    handle_openssl_error("EVP_EncryptUpdate failed");

  if (EVP_EncryptFinal_ex(ctx, ciphertext + c_len, &f_len) != 1)
    handle_openssl_error("EVP_EncryptFinal_ex failed");

  len = c_len + f_len;
  return uc_to_str(ciphertext, len);
}

std::string decrypt_symmetric(EVP_CIPHER_CTX *ctx, std::string_view ciphertext) {
  int len;
  auto cipher_length = (int)ciphertext.length();
  /* plaintext will always be equal to or lesser than length of ciphertext*/
  unsigned char plaintext[cipher_length];

  /* allows reusing of 'ctx' for multiple encryption cycles */
  if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, nullptr, nullptr) != 1)
    handle_openssl_error("EVP_DecryptInit_ex failed");

  if (EVP_DecryptUpdate(ctx, plaintext, &len, (const std::uint8_t *)ciphertext.data(), cipher_length) != 1)
    handle_openssl_error("EVP_DecryptUpdate failed");

  int plaintext_len = len;
  if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) != 1)
    handle_openssl_error("EVP_DecryptFinal_ex failed");

  plaintext_len += len;
  return uc_to_str(plaintext, plaintext_len);
}

std::pair<std::string, std::string> encrypt_authenticated(EVP_CIPHER_CTX *ctx, std::string_view plaintext) {
  /* max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE -1 bytes */
  auto len = (int)plaintext.size();
  int c_len = len + AES_BLOCK_SIZE;
  int f_len = 0;
  unsigned char ciphertext[c_len];
  unsigned char c_tag[AES_GCM_TAG_SIZE];

  /* allows reusing of 'ctx' for multiple encryption cycles */
  if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, nullptr) != 1)
    handle_openssl_error("EVP_EncryptInit_ex failed");

  // Encrypt into the caller's buffer
  if (EVP_EncryptUpdate(ctx, ciphertext, &c_len, (const std::uint8_t *)plaintext.data(), len) != 1)
    handle_openssl_error("EVP_EncryptUpdate failed");

  // GCM encryption won't ever fill ciphertext here but we have to call it anyway
  if (EVP_EncryptFinal_ex(ctx, ciphertext + c_len, &f_len) != 1)
    handle_openssl_error("EVP_EncryptFinal_ex failed");

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_GCM_TAG_SIZE, c_tag) != 1)
    handle_openssl_error("EVP_CTRL_GCM_GET_TAG failed");

  len = c_len + f_len;
  std::string encrypted_str = uc_to_str(ciphertext, len);
  std::string tag = uc_to_str(c_tag, AES_GCM_TAG_SIZE);
  return {encrypted_str, tag};
}

std::string decrypt_authenticated(EVP_CIPHER_CTX *ctx, std::string_view ciphertext, std::string_view tag) {
  int len;
  auto cipher_length = (int)ciphertext.length();
  /* plaintext will always be equal to or lesser than length of ciphertext*/
  unsigned char plaintext[cipher_length];

  /* allows reusing of 'ctx' for multiple encryption cycles */
  if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, nullptr, nullptr) != 1)
    handle_openssl_error("EVP_DecryptInit_ex failed");

  if (EVP_DecryptUpdate(ctx, plaintext, &len, (const std::uint8_t *)ciphertext.data(), cipher_length) != 1)
    handle_openssl_error("EVP_DecryptUpdate failed");

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_GCM_TAG_SIZE, const_cast<char *>(tag.data())) != 1)
    handle_openssl_error("EVP_CTRL_GCM_SET_TAG failed");

  int plaintext_len = len;
  if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) != 1)
    handle_openssl_error("EVP_DecryptFinal_ex failed");

  plaintext_len += len;
  return uc_to_str(plaintext, plaintext_len);
}

} // namespace aes