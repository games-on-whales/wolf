#pragma once

#include <string>
#include <openssl/x509.h>

namespace crypto {

std::string sha256(const std::string str);
std::string pem(const X509 &x509);
std::string str_to_hex(const std::string &input);
std::string hex_to_str(const std::string &hex, bool reverse);

} // namespace crypto