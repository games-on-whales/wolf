#pragma once

#include <moonlight/crypto.hpp>
#include <moonlight/protocol.hpp>

#include <data-structures.hpp>
#include <helpers.cpp>
#include <x509.cpp>

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
  auto clientId = get_header(headers, "uniqueid");
  if (!clientId) {
    logs::log(logs::warning, "Received serverinfo request without uuid");
    server_error<T>(response);
    return;
  }

  auto xml = moonlight::serverinfo(*state.config,
                                   false, // TODO: isServerBusy
                                   -1,    // TODO: current_appid
                                   *state.display_modes,
                                   clientId.value());

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

std::optional<PairCache> get_client_cache(const LocalState &state, const std::string &clientID) {
  auto search_result = std::find_if(state.pairing_cache->begin(),
                                    state.pairing_cache->end(),
                                    [clientID](const PairCache &c) { return c.client_id == clientID; });
  if (search_result != state.pairing_cache->end()) {
    return *search_result;
  } else {
    return std::nullopt;
  }
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
  auto clientID = get_header(headers, "uniqueid");

  // PHASE 1
  if (clientID && salt && client_cert_str) {
    std::string user_pin;
    std::cout << "Insert pin:" << std::endl;
    std::getline(std::cin, user_pin);

    auto [xml, aes_key] = moonlight::pair_get_server_cert(user_pin, salt.value(), *state.server_cert);

    auto client_cert_parsed = crypto::hex_to_str(client_cert_str.value(), true);
    state.pairing_cache->push_back(PairCache{clientID.value(), client_cert_parsed, aes_key});

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  auto client_cache = get_client_cache(state, clientID.value());

  // PHASE 2
  auto client_challenge = get_header(headers, "clientchallenge");
  if (client_cache && clientID && client_challenge) {

    auto server_cert_signature = x509::get_cert_signature(state.server_cert);
    auto [xml, server_secret] =
        moonlight::pair_send_server_challenge(client_cache->aes_key, client_challenge.value(), server_cert_signature);

    // TODO: save back server_secret

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }
}

} // namespace endpoints