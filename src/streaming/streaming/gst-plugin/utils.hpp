#pragma once
#include <algorithm> // for std::reverse
#include <iostream>
#include <memory>  // for std::unique_ptr
#include <numeric> // for std::iota

/**
 * Turns a raw pointer to data into a simple range view over the data
 */
template <typename T> class data_view {

public:
  data_view(T *data, std::size_t size) : m_data{data}, m_size{size} {}

  const T *begin() const {
    return m_data;
  }
  const T *end() const {
    return m_data + m_size;
  }

  T *begin() {
    return m_data;
  }

  T *end() {
    return m_data + m_size;
  }

  [[nodiscard]] std::size_t size() const {
    return m_size;
  }

private:
  T *m_data;
  std::size_t m_size;
};