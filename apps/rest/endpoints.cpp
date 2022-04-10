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
  auto client_id = get_header(headers, "uniqueid");
  if (!client_id) {
    logs::log(logs::warning, "Received serverinfo request without uuid");
    server_error<T>(response);
    return;
  }

  auto xml = moonlight::serverinfo(*state.config,
                                   false, // TODO: isServerBusy
                                   -1,    // TODO: current_appid
                                   *state.display_modes,
                                   client_id.value());

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
  auto client_id = get_header(headers, "uniqueid");
  auto client_ip = request->local_endpoint().address().to_string();

  // PHASE 1
  if (client_id && salt && client_cert_str) {
    std::string user_pin;
    std::cout << "Insert pin:" << std::endl;
    std::getline(std::cin, user_pin); // TODO: async PIN?

    auto [xml, aes_key] = moonlight::pair_get_server_cert(user_pin, salt.value(), *state.server_cert);

    auto client_cert_parsed = crypto::hex_to_str(client_cert_str.value(), true);

    (*state.pairing_cache)[client_id.value() + client_ip] =
        PairCache{client_id.value(), client_cert_parsed, aes_key, std::nullopt, std::nullopt};

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  auto client_cache_it = state.pairing_cache->find(client_id.value() + client_ip);
  if (client_cache_it == state.pairing_cache->end()) {
    logs::log(logs::warning, "Unable to find {} {} in the pairing cache", client_id.value(), client_ip);
    return;
  }
  PairCache client_cache = client_cache_it->second;

  // PHASE 2
  auto client_challenge = get_header(headers, "clientchallenge");
  if (client_challenge) {

    auto server_cert_signature = x509::get_cert_signature(state.server_cert);
    auto [xml, server_secret] =
        moonlight::pair_send_server_challenge(client_cache.aes_key, client_challenge.value(), server_cert_signature);

    client_cache.server_secret = server_secret;
    (*state.pairing_cache)[client_id.value() + client_ip] = client_cache;

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  // PHASE 3
  auto server_challenge = get_header(headers, "serverchallengeresp");
  if (server_challenge) {

    auto [xml, client_hash] = moonlight::pair_get_client_hash(
        client_cache.aes_key,
        client_cache.server_secret.value(),
        server_challenge.value(),
        x509::get_pkey_content(const_cast<EVP_PKEY *>(state.server_pkey)));

    client_cache.client_hash = client_hash;
    (*state.pairing_cache)[client_id.value() + client_ip] = client_cache;

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  // PHASE 4
  auto clientsecret = get_header(headers, "clientpairingsecret");
  if (clientsecret) {
    // TODO:
  }
}

} // namespace endpoints