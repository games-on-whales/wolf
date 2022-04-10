#include <openssl/err.h>
#include <stdexcept>

void handle_openssl_error(const std::string &msg) {
  ERR_print_errors_fp(stderr);
  throw std::runtime_error(msg);
}