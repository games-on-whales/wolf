#pragma once

#include <openssl/aes.h>
#include <openssl/x509.h>
#include <string>

namespace crypto {

std::string sha256(const std::string &str);
std::string pem(const X509 &x509);
std::string str_to_hex(const std::string &input);
std::string hex_to_str(const std::string &hex, bool reverse);
std::string random(int length);

/**
 * Encrypt the given msg using AES ecb at 128 bit
 *
 * @param msg: the message to be encrypted
 * @param enc_key: the key used for encryption
 * @param iv: optional, if not provided a random one will be generated
 * @param padding: optional, enables or disables padding
 * @return: the encrypted string
 */
std::string aes_encrypt_ecb(const std::string &msg,
                            const std::string &enc_key,
                            const std::string &iv = random(AES_BLOCK_SIZE),
                            bool padding = false);

/**
 * Decrypt the given msg using AES ecb at 128 bit
 *
 * @param msg: the message to be encrypted
 * @param enc_key: the key used for encryption
 * @param iv: optional, if not provided a random one will be generated
 * @param padding: optional, enables or disables padding
 * @return: the decrypted string
 */
std::string aes_decrypt_ecb(const std::string &msg,
                            const std::string &enc_key,
                            const std::string &iv = random(AES_BLOCK_SIZE),
                            bool padding = false);

/**
 * Will sign the given message using the private key
 * @param msg: the message to be signed
 * @param private_key: the key used for signing
 * @return: the signature binary data
 */
std::string sign(const std::string &msg, const std::string &private_key);

/**
 * Will verify that the signature for the given message has been generated by the public_key
 * @param msg: the message that was originally signed
 * @param signature: the signature data
 * @param public_key: the public key, used to verify the signature
 * @return: true if the signature is correct, false otherwise.
 */
bool verify(const std::string &msg, const std::string &signature, const std::string &public_key);

} // namespace crypto