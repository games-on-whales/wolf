#include "utils.hpp"
#include <cstring>
#include <openssl/evp.h>
#include <openssl/pem.h>

namespace signature {
using EVP_MD_CTX_ptr = std::unique_ptr<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;

std::string sign(const std::string &msg, EVP_PKEY *key_data, const EVP_MD *digest_type) {
  EVP_MD_CTX_ptr ctx(EVP_MD_CTX_create(), ::EVP_MD_CTX_free);
  int msg_size = (int)msg.size();

  if (EVP_DigestSignInit(ctx.get(), nullptr, digest_type, nullptr, key_data) != 1)
    handle_openssl_error("EVP_DigestSignInit failed");

  if (EVP_DigestSignUpdate(ctx.get(), msg.data(), msg_size) != 1)
    handle_openssl_error("EVP_DigestSignUpdate failed");

  std::size_t digest_size = 256;
  unsigned char digest[digest_size];

  if (EVP_DigestSignFinal(ctx.get(), digest, &digest_size) != 1)
    handle_openssl_error("EVP_DigestSignFinal failed");

  return {reinterpret_cast<char *>(digest), digest_size};
}

bool verify(const std::string &msg, const std::string &signature, EVP_PKEY *key_data, const EVP_MD *digest_type) {
  EVP_MD_CTX_ptr ctx(EVP_MD_CTX_create(), ::EVP_MD_CTX_free);
  int msg_size = (int)msg.size();

  if (EVP_DigestVerifyInit(ctx.get(), nullptr, digest_type, nullptr, key_data) != 1)
    handle_openssl_error("EVP_DigestSignInit failed");

  if (EVP_DigestVerifyUpdate(ctx.get(), msg.data(), msg_size) != 1)
    handle_openssl_error("EVP_DigestSignUpdate failed");

  std::size_t signature_size = signature.size();
  return EVP_DigestVerifyFinal(ctx.get(), (const std::uint8_t *)signature.data(), signature_size) == 1;
}

using EVP_PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)>;

EVP_PKEY_ptr create_key(const std::string &k, bool is_private) {
  BIO *bio = BIO_new(BIO_s_mem());

  if (BIO_write(bio, k.data(), k.size()) <= 0)
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
} // namespace signature
