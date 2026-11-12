#pragma once

#include <memory>
#include <string>

/**
 * Prints (and clears) the pending OpenSSL error queue, then throws a std::runtime_error with the given message.
 * Call it whenever an OpenSSL call fails: its output buffers or context can't be trusted afterwards.
 */
[[noreturn]] void handle_openssl_error(const std::string &msg);
std::string uc_to_str(unsigned char *uc, int len);