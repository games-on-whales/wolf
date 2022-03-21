#pragma once

#include <moonlight/crypto.hpp>
#include <moonlight/protocol.hpp>

#include <x509.cpp>
#include <helpers.cpp>
#include <data-structures.hpp>

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
                                   false, // TODO: isServerBusy
                                   -1,    // TODO: current_appid
                                   *state.display_modes.get(),
                                   clientId.value());       

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

/**
 * @brief Moonlight protocol phase 2: GET /pair
 */
template <class T>
void pair(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response,
          std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request,
          const LocalState &state) {
  log_req<T>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();

  auto salt = get_header(headers, "salt");
  auto client_cert_str = get_header(headers, "clientcert");
  if (salt && client_cert_str) {
    std::string user_pin;
    std::cout << "Insert pin:" << std::endl;
    std::getline(std::cin, user_pin);

    auto [xml, key] = moonlight::pair_get_server_cert(user_pin, salt.value(), *state.server_cert);

    auto client_cert_parsed = crypto::hex_to_str(client_cert_str.value(), true);
    auto client_cert = x509::cert_from_string(client_cert_parsed);
    // TODO: save key and client_cert for later

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
  }

  auto client_challenge = get_header(headers, "clientchallenge");
  if (client_challenge) {

    // TODO: retrieve key and client_cert
  }
}

} // namespace endpoints