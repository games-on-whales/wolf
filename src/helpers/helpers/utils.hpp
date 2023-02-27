#pragma once
#include <algorithm>
#include <range/v3/view.hpp>
#include <sstream>
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
 * Join a list of strings into a single string with separator in between elements
 */
template <class T> inline std::string join(const std::vector<T> &vec, std::string_view separator) {
  return vec | views::join(separator);
}

} // namespace utils