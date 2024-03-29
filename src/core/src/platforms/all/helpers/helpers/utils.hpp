#pragma once
#include <algorithm>
#include <boost/endian.hpp>
#include <range/v3/view.hpp>
#include <sstream>
#include <stdlib.h>
#include <string>

namespace utils {

using namespace ranges;

/**
 * Since we can't switch() on strings we use hashes instead.
 * Adapted from https://stackoverflow.com/a/46711735/3901988
 */
constexpr uint32_t hash(const std::string_view data) noexcept {
  uint32_t hash = 5385;
  for (const auto &e : data)
    hash = ((hash << 5) + hash) + e;
  return hash;
}

/**
 * Returns the sub_string between the two input characters
 */
inline std::string_view sub_string(std::string_view str, char begin, char end) {
  auto s = str.find(begin);
  auto e = str.find(end, s);
  return str.substr(s + 1, e - s - 1);
}

inline std::string to_lower(std::string_view str) {
  std::string result(str);
  std::transform(str.begin(), str.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
  return result;
}

/**
 * Splits the given string into an array of strings at any given separator
 */
inline std::vector<std::string_view> split(std::string_view str, char separator) {
  return str                                                                                              //
         | views::split(separator)                                                                        //
         | views::transform([](auto &&ptrs) { return std::string_view(&*ptrs.begin(), distance(ptrs)); }) //
         | to_vector;                                                                                     //
}

/**
 * Copies out a string_view content back to a string
 * This differs from using .data() since it'll add the terminator
 */
inline std::string to_string(std::string_view str) {
  return {str.begin(), str.end()};
}

inline const char *get_env(const char *tag, const char *def = nullptr) noexcept {
  const char *ret = std::getenv(tag);
  return ret ? ret : def;
}

/**
 * Join a list of strings into a single string with separator in between elements
 */
inline std::string join(const std::vector<std::string> &vec, std::string_view separator) {
  return vec | view::join(separator) | to<std::string>();
}

/**
 * netfloat is just a little-endian float in byte form for network transmission.
 */
typedef uint8_t netfloat[4];

/**
 * @brief Converts a little-endian netfloat to a native endianness float.
 * @param f Netfloat value.
 * @return Float value.
 */
inline float from_netfloat(const utils::netfloat &f) {
  return boost::endian::endian_load<float, sizeof(float), boost::endian::order::little>(f);
}

inline std::string base64_encode(const std::string &in) {
  std::string out;

  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

inline std::string
map_to_string(const std::map<std::string, std::string> &m, char val_separator = '=', char row_separator = '\0') {
  std::stringstream ss;

  for (auto it = m.cbegin(); it != m.cend(); it++) {
    ss << it->first << val_separator << it->second << row_separator;
  }

  return ss.str();
}

} // namespace utils