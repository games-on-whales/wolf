#pragma once

#include <rest/helpers.cpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>

#include <boost/property_tree/ptree.hpp>
namespace pt = boost::property_tree;

namespace endpoints {

/**
 * @brief This is the default endpoint when no condition is matched.
 * returns a 404 response
 */
template <class T>
void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response,
               std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  log_req<T>(request);

  pt::ptree tree;
  tree.put("root.<xmlattr>.status_code", 404);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_not_found, tree);
}

} // namespace endpoints