/**
 * Implements safe stacktrace dumping and loading.
 *
 * Only uses async-signal-safe functions for dumping the stacktrace; read here for more info:
 * https://man7.org/linux/man-pages/man7/signal-safety.7.html
 */
#pragma once

#include <cpptrace/cpptrace.hpp>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <unistd.h>

using namespace std::string_literals;

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
  int fd = open(file_name.c_str(), O_RDONLY);
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

static std::string backtrace_file_src() {
  return std::string(utils::get_env("WOLF_CFG_FOLDER", ".")) + "/backtrace.dump"s;
}

/**
 * Keep this as small as possible, make sure to only use async-signal-safe functions
 */
static void shutdown_handler(int signum) {
  if (signum == SIGABRT || signum == SIGSEGV) {
    auto stack_file = backtrace_file_src();
    safe_dump_stacktrace_to(stack_file);
  }
  exit(signum);
}

/**
 * @brief: if an exception was raised we should have created a dump file, here we can pretty print it
 */
static void check_exceptions() {
  auto stack_file = backtrace_file_src();
  if (std::filesystem::exists(stack_file)) {
    if (auto object_trace = load_stacktrace_from(stack_file)) {
      object_trace->resolve().print();
    }
    auto now = std::chrono::system_clock::now();
    std::filesystem::rename(
        stack_file,
        fmt::format("{}/backtrace.{:%Y-%m-%d-%H-%M-%S}.dump", utils::get_env("WOLF_CFG_FOLDER", "."), now));
  }
}

static void on_terminate() {
  if (auto eptr = std::current_exception()) {
    try {
      std::rethrow_exception(eptr);
    } catch (const std::exception &e) {
      logs::log(logs::error, "Unhandled exception: {}", e.what());
    }
  }

  shutdown_handler(SIGABRT);
}
