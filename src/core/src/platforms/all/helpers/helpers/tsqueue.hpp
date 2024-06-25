#pragma once

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>

/**
 * Thread safe queue
 */
template <typename T> class TSQueue {
private:
  // Underlying queue
  std::queue<T> m_queue;

  // mutex for thread synchronisation
  std::mutex m_mutex;

  // Condition variable for signalling
  std::condition_variable m_cond;

public:
  TSQueue() = default;

  /**
   * Pushes an element to the queue
   */
  void push(T item) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_queue.push(item);
    m_cond.notify_all();
  }

  /**
   * Pops an element off the queue
   * @param timeout it'll wait up until this time for an element to be available
   * @return the element if it was available, empty optional otherwise
   */
  std::optional<T> pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
    std::unique_lock<std::mutex> lock(m_mutex);

    // wait until queue is not empty or timeout
    auto res = m_cond.wait_for(lock, timeout, [this]() { return !m_queue.empty(); });

    // if timeout returns empty optional
    if (!res) {
      return {};
    }

    // retrieve item
    auto item = std::move(m_queue.front());
    m_queue.pop();
    return item;
  }
};