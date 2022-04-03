#pragma once

#include <memory>
#include <openssl/err.h>
#include <stdexcept>
#include <string>

void handle_openssl_error(const std::string &msg) {
  ERR_print_errors_fp(stderr);
  throw std::runtime_error(msg);
}

std::unique_ptr<unsigned char[]> to_unsigned(const std::string &str) {
  auto uc_str = std::make_unique<unsigned char[]>(str.size());
  std::copy(str.begin(), str.end(), uc_str.get());
  uc_str[str.length()] = 0;
  return uc_str;
}