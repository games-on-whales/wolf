#include <crypto/utils.hpp>
#include <memory>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace aes {
using CIPHER_CTX_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

CIPHER_CTX_ptr init(const evp_cipher_st *chiper,
                    const std::string &key_data,
                    const std::string &iv,
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

std::string encrypt(EVP_CIPHER_CTX *ctx, const std::string &plaintext) {
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
  return {reinterpret_cast<char *>(ciphertext), static_cast<unsigned long>(len)};
}

std::string decrypt(EVP_CIPHER_CTX *ctx, const std::string &ciphertext) {
  int len;
  auto cipher_length = (int)ciphertext.length();
  /* plaintext will always be equal to or lesser than length of ciphertext*/
  auto plaintext = static_cast<unsigned char *>(malloc(cipher_length));

  /* allows reusing of 'ctx' for multiple encryption cycles */
  if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, nullptr, nullptr) != 1)
    handle_openssl_error("EVP_DecryptInit_ex failed");

  if (EVP_DecryptUpdate(ctx, plaintext, &len, (const std::uint8_t *)ciphertext.data(), cipher_length) != 1)
    handle_openssl_error("EVP_DecryptUpdate failed");

  int plaintext_len = len;
  if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) != 1)
    handle_openssl_error("EVP_DecryptFinal_ex failed");

  plaintext_len += len;
  return {reinterpret_cast<char *>(plaintext), static_cast<unsigned long>(plaintext_len)};
}

} // namespace aes