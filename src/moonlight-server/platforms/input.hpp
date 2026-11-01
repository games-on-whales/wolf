#pragma once

#include <codecvt>
#include <core/input.hpp>
#include <events/events.hpp>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>

namespace wolf::platforms::input {
/**
 * Takes an UTF-32 encoded string and returns a hex string representation of the bytes (uppercase)
 *
 * ex: ['💩'] = "1F4A9" // see UTF encoding at https://www.compart.com/en/unicode/U+1F4A9
 *
 * adapted from: https://stackoverflow.com/a/7639754
 */
static std::string to_hex(const std::u32string &str) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(4);
  for (char32_t c : str) {
    ss << static_cast<uint32_t>(c);
  }

  std::string hex_unicode(ss.str());
  std::transform(hex_unicode.begin(), hex_unicode.end(), hex_unicode.begin(), ::toupper);
  return hex_unicode;
}

using namespace wolf::core;

void paste_utf(events::KeyboardTypes &keyboard, const std::basic_string<char32_t> &utf32);
} // namespace wolf::platforms::input