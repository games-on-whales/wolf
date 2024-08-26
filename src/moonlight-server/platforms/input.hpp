#pragma once

#include <core/input.hpp>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <state/data-structures.hpp>

namespace wolf::platforms::input {
/**
 * Takes an UTF-32 encoded string and returns a hex string representation of the bytes (uppercase)
 *
 * ex: ['💩'] = "1F4A9" // see UTF encoding at https://www.compart.com/en/unicode/U+1F4A9
 *
 * adapted from: https://stackoverflow.com/a/7639754
 */
static std::string to_hex(const std::basic_string<char32_t> &str) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (const auto &ch : str) {
    ss << (char)(ch >> 16);
    ss << (char)(ch >> 8);
    ss << (char)ch;
  }

  std::string hex_unicode(ss.str());
  std::transform(hex_unicode.begin(), hex_unicode.end(), hex_unicode.begin(), ::toupper);
  return hex_unicode;
}

void paste_utf(state::KeyboardTypes &keyboard, const std::basic_string<char32_t> &utf32);
} // namespace wolf::platforms::input