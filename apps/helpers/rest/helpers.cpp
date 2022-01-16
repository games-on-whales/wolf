#pragma once

#include <sstream>
#include <string_view>
using namespace std::literals;

#include <log/logger.cpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace pt = boost::property_tree;

/**
 * @brief C++ way to get which kind of tunnel T is used (http or https)
 */
template <class T> struct tunnel;
template <> struct tunnel<SimpleWeb::HTTPS> { static auto constexpr to_string = "https"sv; };
template <> struct tunnel<SimpleWeb::HTTP> { static auto constexpr to_string = "http"sv; };

/**
 * @brief Log the request
 */
template <class T> void log_req(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  Logger::log(debug, "[{}] {}://{}{}", request->method, tunnel<T>::to_string, request->local_endpoint(), request->path);
  Logger::log(trace, "Header: {}", request->parse_query_string());
}

/**
 * @brief send the XML as a response with the specified status_code
 */
template <class T>
void send_xml(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response,
              SimpleWeb::StatusCode status_code,
              pt::ptree xml) {
  std::ostringstream data;
  pt::write_xml(data, xml);
  response->write(status_code, data.str());
  response->close_connection_after_response = true;
}