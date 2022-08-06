#pragma once
#include <range/v3/view.hpp>
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
std::string_view sub_string(std::string_view str, char begin, char end) {
  auto s = str.find(begin);
  auto e = str.find(end, s);
  return str.substr(s + 1, e - s - 1);
}

/**
 * Splits the given string into an array of strings at any given separator
 */
std::vector<std::string_view> split(std::string_view str, char separator) {
  return str                                                                                             //
         | view::split(separator)                                                                        //
         | view::transform([](auto &&ptrs) { return std::string_view(&*ptrs.begin(), distance(ptrs)); }) //
         | to_vector;                                                                                    //
}

} // namespace utils