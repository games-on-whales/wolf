#include <cstdio>
#include <cstring>
#include <fstream>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <optional>
#include <stdexcept>
#include <string>

namespace x509 {

EVP_PKEY *generate_key() {
  auto pkey = EVP_PKEY_new();
  if (!pkey) {
    throw std::runtime_error("Unable to create EVP_PKEY structure.");
  }

  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (!ctx) {
    throw std::runtime_error("Unable to generate 2048-bit RSA key.");
  }
  if (EVP_PKEY_keygen_init(ctx) <= 0) {
    throw std::runtime_error("Unable to generate 2048-bit RSA key.");
  }
  if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
    throw std::runtime_error("Unable to generate 2048-bit RSA key.");
  }
  if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
    throw std::runtime_error("Unable to generate 2048-bit RSA key.");
  }
  EVP_PKEY_CTX_free(ctx);

  return pkey;
}

X509 *generate_x509(EVP_PKEY *pkey) {
  /* Allocate memory for the X509 structure. */
  X509 *x509 = X509_new();
  if (!x509) {
    throw std::runtime_error("Unable to create X509 structure.");
  }

  /* Set the serial number. */
  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1); // Set the serial number.
  X509_set_version(x509, 2);

  auto valid_years = 630720000L; // This certificate is valid for 20 years
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), valid_years);

  /* Set the public key for our certificate. */
  X509_set_pubkey(x509, pkey);

  /* We want to copy the subject name to the issuer name. */
  X509_NAME *name = X509_get_subject_name(x509);

  /* Set the country code and common name. */
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"IT", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char *)"GamesOnWhales", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);
  X509_set_issuer_name(x509, name);

  /* Actually sign the certificate with our key. */
  if (!X509_sign(x509, pkey, EVP_sha256())) {
    throw std::runtime_error("Error signing certificate.");
  }

  return x509;
}

X509 *cert_from_string(std::string_view cert) {
  BIO *bio;
  X509 *certificate;

  bio = BIO_new(BIO_s_mem());
  BIO_puts(bio, cert.data());
  certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

  BIO_free(bio);
  return certificate;
}

X509 *cert_from_file(std::string_view cert_path) {
  X509 *certificate;
  BIO *bio;

  bio = BIO_new(BIO_s_file());
  if (BIO_read_filename(bio, cert_path.data()) <= 0) {
    throw std::runtime_error("Error reading certificate");
  }
  certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

  BIO_free(bio);
  return certificate;
}

EVP_PKEY *pkey_from_file(std::string_view pkey_path) {
  EVP_PKEY *pkey;
  BIO *bio;

  bio = BIO_new(BIO_s_file());
  if (BIO_read_filename(bio, pkey_path.data()) <= 0) {
    throw std::runtime_error("Error reading private key");
  }
  pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);

  BIO_free(bio);
  return pkey;
}

bool write_to_disk(EVP_PKEY *pkey, std::string_view pkey_filename, X509 *x509, std::string_view cert_filename) {
  /* Open the PEM file for writing the key to disk. */
  FILE *pkey_file = fopen(pkey_filename.data(), "wb");
  if (!pkey_file) {
    throw std::runtime_error("Unable write file to disk.");
  }

  /* Write the key to disk. */
  bool ret = PEM_write_PrivateKey(pkey_file, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  fclose(pkey_file);

  if (!ret) {
    throw std::runtime_error("Unable to write file to disk.");
  }

  /* Open the PEM file for writing the certificate to disk. */
  FILE *x509_file = fopen(cert_filename.data(), "wb");
  if (!x509_file) {
    throw std::runtime_error("Unable to write file to disk.");
  }

  /* Write the certificate to disk. */
  ret = PEM_write_X509(x509_file, x509);
  fclose(x509_file);

  if (!ret) {
    throw std::runtime_error("Unable to write {} to disk.");
  }

  return true;
}

bool cert_exists(std::string_view pkey_filename, std::string_view cert_filename) {
  std::fstream pkey_fs(pkey_filename.data());
  std::fstream cert_fs(cert_filename.data());
  return pkey_fs.good() && cert_fs.good();
}

std::string get_cert_signature(const X509 *cert) {
  const ASN1_BIT_STRING *asn1 = nullptr;
  X509_get0_signature(&asn1, nullptr, cert);

  return {(const char *)asn1->data, (std::size_t)asn1->length};
}

std::string get_cert_pem(const X509 &x509) {
  X509 *cert_ptr = const_cast<X509 *>(&x509);
  BIO *bio_out = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(bio_out, cert_ptr);
  BUF_MEM *bio_buf;
  BIO_get_mem_ptr(bio_out, &bio_buf);
  std::string pem = std::string(bio_buf->data, bio_buf->length);
  BIO_free(bio_out);
  return pem;
}

std::string get_key_content(EVP_PKEY *pkey, bool private_key) {
  BIO *bio;

  bio = BIO_new(BIO_s_mem());

  if (private_key) {
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  } else {
    PEM_write_bio_PUBKEY(bio, pkey);
  }

  const int keylen = BIO_pending(bio);
  char *key = (char *)calloc(keylen + 1, 1);
  BIO_read(bio, key, keylen);
  BIO_free_all(bio);

  std::string result(key, static_cast<size_t>(keylen));
  free(key);
  return result;
}

std::string get_pkey_content(EVP_PKEY *pkey) {
  return get_key_content(pkey, true);
}

std::string get_cert_public_key(X509 *cert) {
  auto pkey = X509_get_pubkey(cert);
  return get_key_content(pkey, false);
}

void cleanup(EVP_PKEY *pkey, X509 *cert) {
  EVP_PKEY_free(pkey);
  X509_free(cert);
}

/**
 * @brief: adapted from Sunshine
 */
static int openssl_verify_cb(int ok, X509_STORE_CTX *ctx) {
  int err_code = X509_STORE_CTX_get_error(ctx);

  switch (err_code) {
  // TODO: Checking for X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY is a temporary workaround to get
  // moonlight-embedded to work on the raspberry pi
  case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    return 1;

  // Expired or not-yet-valid certificates are fine. Sometimes Moonlight is running on embedded devices
  // that don't have accurate clocks (or haven't yet synchronized by the time Moonlight first runs).
  // This behavior also matches what GeForce Experience does.
  case X509_V_ERR_CERT_NOT_YET_VALID:
  case X509_V_ERR_CERT_HAS_EXPIRED:
    return 1;

  default:
    return ok;
  }
}

/**
 * @brief: adapted from Sunshine
 */
std::optional<std::string> verification_error(X509 *paired_cert, X509 *untrusted_cert) {
  auto x509_store{X509_STORE_new()};
  X509_STORE_add_cert(x509_store, paired_cert);

  auto _cert_ctx{X509_STORE_CTX_new()};

  X509_STORE_CTX_init(_cert_ctx, x509_store, untrusted_cert, nullptr);
  X509_STORE_CTX_set_verify_cb(_cert_ctx, openssl_verify_cb);

  // We don't care to validate the entire chain for the purposes of client auth.
  // Some versions of clients forked from Moonlight Embedded produce client certs
  // that OpenSSL doesn't detect as self-signed due to some X509v3 extensions.
  X509_STORE_CTX_set_flags(_cert_ctx, X509_V_FLAG_PARTIAL_CHAIN);

  auto err = X509_verify_cert(_cert_ctx);
  X509_STORE_free(x509_store);

  if (err == 1) {
    X509_STORE_CTX_free(_cert_ctx);
    return std::nullopt;
  }

  int err_code = X509_STORE_CTX_get_error(_cert_ctx);
  X509_STORE_CTX_free(_cert_ctx);
  return X509_verify_cert_error_string(err_code);
}

} // namespace x509