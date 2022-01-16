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

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/ostream.h>

using namespace boost::log::trivial;

namespace Logger {
/**
 * @brief first time Boost log system initialization
 *
 * @param min_log_level: The minum log level to be reported, anything below this will not be printed
 */
void init(severity_level min_log_level) {
  /* init boost log
   * 1. Add common attributes
   * 2. set log filter to trace
   */
  boost::log::add_common_attributes();
  boost::log::core::get()->set_filter(boost::log::trivial::severity >= min_log_level);

  /* log formatter:
   * [TimeStamp] - Severity Level > Log message
   */
  auto fmtTimeStamp =
      boost::log::expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%d-%m-%Y %H:%M:%S");
  auto fmtSeverity = boost::log::expressions::attr<boost::log::trivial::severity_level>("Severity");
  boost::log::formatter logFmt = boost::log::expressions::format("[%1%] - %2% > %3%") % fmtTimeStamp % fmtSeverity %
                                 boost::log::expressions::smessage;

  /* console sink */
  auto consoleSink = boost::log::add_console_log(std::clog);
  consoleSink->set_formatter(logFmt);
}

/**
 * @brief output a log message with optional format
 *
 * @param lv: log level
 * @param format_str: a valid fmt::format string
 * @param args: optional additional args to be formatted
 */
template <typename S, typename... Args> void log(severity_level lv, const S &format_str, const Args &...args) {
  auto msg = fmt::format(format_str, args...);
  boost::log::sources::severity_logger<severity_level> lg;

  BOOST_LOG_SEV(lg, lv) << msg;
}
} // namespace Logger