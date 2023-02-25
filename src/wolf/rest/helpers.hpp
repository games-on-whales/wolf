#pragma once

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <helpers/logger.hpp>
#include <moonlight/protocol.hpp>
#include <rest/rest.hpp>
#include <string_view>

using namespace std::literals;
using XML = moonlight::XML;
namespace pt = boost::property_tree;

inline std::string xml_to_str(const XML &xml) {
  std::stringstream ss;
  pt::write_xml(ss, xml);
  return ss.str();
}

template <class T> std::string get_client_ip(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  return request->remote_endpoint().address().to_string();
}

/**
 * @brief Log the request
 */
template <class T> inline void log_req(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  logs::log(logs::debug,
            "{} [{}] {}://{}{}",
            get_client_ip<T>(request),
            request->method,
            std::is_same_v<SimpleWeb::HTTP, T> ? "HTTP" : "HTTPS",
            request->local_endpoint().address().to_string(),
            request->path);
  logs::log(logs::trace, "Header: {}", request->parse_query_string());
}

/**
 * @brief send the XML as a response with the specified status_code
 */
template <class T>
inline void send_xml(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response,
                     SimpleWeb::StatusCode status_code,
                     const XML &xml) {
  std::ostringstream data;
  pt::write_xml(data, xml);
  logs::log(logs::trace, "Response: {}", xml_to_str(xml));
  response->write(status_code, data.str());
  response->close_connection_after_response = true;
}

/**
 * @brief Get the header value for the supplied key, if present
 *
 * @param headers: the result of doing request->parse_query_string()
 * @param key: the header key name
 * @return std::string if found, NULL otherwise
 */
inline std::optional<std::string> get_header(const SimpleWeb::CaseInsensitiveMultimap &headers, const std::string key) {
  auto it = headers.find(key);
  if (it != headers.end()) {
    return it->second;
  } else {
    return {};
  }
}