#include <openssl/err.h>
#include <stdexcept>
#include <string>

void handle_openssl_error(const std::string &msg) {
  // Drain the whole OpenSSL error queue so that stale errors don't leak into the next call on this thread
  std::string details;
  while (auto err = ERR_get_error()) {
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    details += details.empty() ? ": " : "; ";
    details += buf;
  }
  throw std::runtime_error(msg + details);
}

std::string uc_to_str(unsigned char *uc, int len) {
  return {reinterpret_cast<char *>(uc), static_cast<unsigned long>(len)};
}