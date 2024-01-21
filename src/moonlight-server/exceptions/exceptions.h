#pragma once

#include <cpptrace/cpptrace.hpp>
#include <helpers/logger.hpp>
#include <istream>
#include <ostream>

static void safe_dump_stacktrace_to(std::ostream &out) {
  constexpr std::size_t N = 100;
  cpptrace::frame_ptr buffer[N];
  std::size_t count = cpptrace::safe_generate_raw_trace(buffer, N);
  if (count > 0) {
    out << count << '\n';
    for (std::size_t i = 0; i < count; i++) {
      cpptrace::safe_object_frame frame{};
      cpptrace::get_safe_object_frame(buffer[i], &frame);
      out << frame.address_relative_to_object_start << ' ' << frame.raw_address << ' ';
      out.write(frame.object_path, sizeof(frame.object_path));
      out << '\n';
    }
  }
}

static std::unique_ptr<cpptrace::object_trace> load_stacktrace_from(std::istream &in) {
  cpptrace::object_trace trace{};
  std::size_t count;
  in >> count;
  in.ignore(); // ignore newline
  for (std::size_t i = 0; i < count; i++) {
    cpptrace::safe_object_frame frame{};
    in >> frame.address_relative_to_object_start;
    in.ignore(); // ignore space
    in >> frame.raw_address;
    in.ignore(); // ignore space
    in.read(frame.object_path, sizeof(frame.object_path));
    in.ignore(); // ignore newline
    try {
      trace.frames.push_back(frame.resolve());
    } catch (std::exception &ex) {
      logs::log(logs::debug, "Unable to parse stacktrace frame, skipping");
    }
  }
  return std::make_unique<cpptrace::object_trace>(trace);
}
