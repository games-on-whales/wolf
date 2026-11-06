#include <openssl/err.h>
#include <stdexcept>
#include <string>

void handle_openssl_error(const std::string &msg) {
  ERR_print_errors_fp(stderr);
  throw std::runtime_error(msg);
}

std::string uc_to_str(unsigned char *uc, int len) {
  return {reinterpret_cast<char *>(uc), static_cast<unsigned long>(len)};
}