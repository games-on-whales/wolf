/**
 * Implements safe stacktrace dumping and loading.
 *
 * Only uses async-signal-safe functions for dumping the stacktrace; read here for more info:
 * https://man7.org/linux/man-pages/man7/signal-safety.7.html
 */
#pragma once

#include <cpptrace/cpptrace.hpp>
#include <fcntl.h>
#include <helpers/logger.hpp>
#include <unistd.h>

static void safe_dump_stacktrace_to(const std::string &file_name) {
  constexpr std::size_t N = 100;
  cpptrace::frame_ptr buffer[N];
  std::size_t count = cpptrace::safe_generate_raw_trace(buffer, N);
  if (count > 0) {
    int fd = open(file_name.c_str(), O_WRONLY | O_CREAT | O_DSYNC, 0666);
    if (fd <= 0) {
      return;
    }
    write(fd, reinterpret_cast<char *>(&count), sizeof(count));
    for (std::size_t i = 0; i < count; i++) {
      cpptrace::safe_object_frame frame{};
      cpptrace::get_safe_object_frame(buffer[i], &frame);
      write(fd, &frame, sizeof(frame));
    }
    close(fd);
  }
}

static std::unique_ptr<cpptrace::object_trace> load_stacktrace_from(const std::string &file_name) {
  cpptrace::object_trace trace{};
  std::size_t count;
  int fd = open(file_name.c_str(), O_RDONLY | O_CREAT | O_DSYNC);
  if (fd <= 0) {
    logs::log(logs::warning, "Unable to open stacktrace file {}", file_name);
    return {};
  }
  read(fd, reinterpret_cast<char *>(&count), sizeof(count));
  for (std::size_t i = 0; i < count; i++) {
    cpptrace::safe_object_frame frame{};
    read(fd, &frame, sizeof(frame));
    try {
      trace.frames.push_back(frame.resolve());
    } catch (std::exception &ex) {
      logs::log(logs::debug, "Unable to parse stacktrace frame, skipping");
    }
  }
  return std::make_unique<cpptrace::object_trace>(trace);
}
