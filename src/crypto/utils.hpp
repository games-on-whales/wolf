#pragma once

#include <memory>
#include <string>

void handle_openssl_error(const std::string &msg);
std::unique_ptr<unsigned char[]> to_unsigned(const std::string &str);