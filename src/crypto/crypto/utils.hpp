#pragma once

#include <memory>
#include <string>

void handle_openssl_error(const std::string &msg);
std::string uc_to_str(unsigned char *uc, int len);