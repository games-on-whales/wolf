#pragma once

#include <moonlight/protocol.hpp>

#include <rest/helpers.cpp>
#include <state/data-structures.hpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>

#include <boost/property_tree/ptree.hpp>
namespace pt = boost::property_tree;

namespace endpoints {

/**
 * @brief server error will be the default response when something goes wrong
 */
template <class T> void server_error(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response) {
  pt::ptree xml;
  xml.put("root.<xmlattr>.status_code", 400);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_bad_request, xml);
}

/**
 * @brief This is the default endpoint when no condition is matched.
 * returns a 404 response
 */
template <class T>
void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response,
               std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  log_req<T>(request);

  pt::ptree xml;
  xml.put("root.<xmlattr>.status_code", 404);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_not_found, xml);
}

/**
 * @brief Moonlight protocol phase 1: GET /serverinfo
 */
template <class T>
void serverinfo(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response,
                std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request,
                const LocalState &state) {
  log_req<T>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto clientId = get_header(headers, "uuid");
  if (!clientId) {
    logs::log(logs::warning, "Received serverinfo request without uuid");
    server_error<T>(response);
    return;
  }

  auto xml = moonlight::serverinfo(*state.config.get(),
                                   *state.pair_handler.get(),
                                   false, // TODO:
                                   -1,    // TODO:
                                   *state.display_modes.get(),
                                   clientId.value());

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

} // namespace endpoints