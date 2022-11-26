#pragma once

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/shared_ptr.hpp>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

namespace logs {

using namespace boost::log::trivial;

inline auto get_color(boost::log::trivial::severity_level level) {
  switch (level) {
  case debug:
  case trace:
  case info:
    return "\033[37;1m";
  case warning:
    return "\033[33;1m";
  case error:
  case fatal:
    return "\033[31;1m";
  default:
    return "\033[0m";
  }
}

inline auto get_name(boost::log::trivial::severity_level level) {
  switch (level) {
  case trace:
    return "TRACE";
    break;
  case debug:
    return "DEBUG";
    break;
  case info:
    return "INFO";
    break;
  case warning:
    return "WARN";
    break;
  case error:
    return "ERROR";
    break;
  case fatal:
    return "FATAL";
    break;
  default:
    return "";
    break;
  }
}

/**
 * @brief first time Boost log system initialization
 *
 * @param min_log_level: The minum log level to be reported, anything below this will not be printed
 */
inline void init(severity_level min_log_level) {
  /* init boost log
   * 1. Add common attributes
   * 2. set log filter to trace
   */
  boost::log::add_common_attributes();
  boost::log::core::get()->set_filter(boost::log::trivial::severity >= min_log_level);

  /* console sink */
  auto consoleSink = boost::log::add_console_log(std::clog);
  consoleSink->set_formatter([](boost::log::record_view const &rec, boost::log::formatting_ostream &strm) {
    auto severity = rec[boost::log::trivial::severity];
    auto msg = rec[boost::log::expressions::smessage];
    auto now = std::chrono::system_clock::now();

    strm << get_color(severity.get());
    strm << fmt::format("{:%T} {:<5} | {}", now.time_since_epoch(), get_name(severity.get()), msg.get());
    strm << "\033[0m";
  });
}

/**
 * @brief output a log message with optional format
 *
 * @param lv: log level
 * @param format_str: a valid fmt::format string
 * @param args: optional additional args to be formatted
 */
template <typename S, typename... Args> inline void log(severity_level lv, const S &format_str, const Args &...args) {
  auto msg = fmt::format(format_str, args...);
  boost::log::sources::severity_logger<severity_level> lg;

  BOOST_LOG_SEV(lg, lv) << msg;
}

inline logs::severity_level parse_level(const std::string &level) {
  std::string lvl = level;
  std::transform(level.begin(), level.end(), lvl.begin(), [](unsigned char c) { return std::toupper(c); });
  if (lvl == "TRACE") {
    return logs::trace;
  } else if (lvl == "DEBUG") {
    return logs::debug;
  } else if (lvl == "INFO") {
    return logs::info;
  } else if (lvl == "WARNING") {
    return logs::warning;
  } else if (lvl == "ERROR") {
    return logs::error;
  } else {
    return logs::fatal;
  }
}
} // namespace logs