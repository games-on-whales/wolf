#pragma once

#include <future>
#include <memory>
#include <utility>

#include <rest/custom-https.cpp>
#include <rest/endpoints.cpp>
#include <rest/helpers.cpp>
#include <Simple-Web-Server/server_http.hpp>

using HttpsServer = HTTPSCustomCert;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

namespace HTTPServers {

/**
 * @brief Start the generic server on the specified port
 * @return std::thread: the thread where this server will run
 */
template <typename T> std::thread startServer(SimpleWeb::Server<T> *server, const LocalState &state, int port) {
  server->config.port = port;
  server->config.address = {"0.0.0.0"};
  server->default_resource["GET"] = endpoints::not_found<T>;
  server->default_resource["POST"] = endpoints::not_found<T>;

  server->resource["^/serverinfo$"]["GET"] = [&state](auto resp, auto req) {
    endpoints::serverinfo<T>(resp, req, state);
  };

  server->resource["^/pair$"]["GET"] = [&state](auto resp, auto req) { endpoints::pair<T>(resp, req, state); };

  // HTTPS will have more endpoints
  if (port == state.config->map_port(moonlight::Config::HTTPS_PORT)) {
    // https_server.resource["^/applist$"]["GET"]
    // https_server.resource["^/appasset$"]["GET"]
    // https_server.resource["^/launch$"]["GET"]
    // https_server.resource["^/resume$"]["GET"]
    // https_server.resource["^/cancel$"]["GET"]
  }

  std::promise<unsigned short> server_port;
  std::thread server_thread([&server, &server_port]() {
    // Start server
    server->start([&server_port](unsigned short port) { server_port.set_value(port); });
  });
  logs::log(logs::debug, "{} server listening on port: {} ", tunnel<T>::to_string, server_port.get_future().get());

  return server_thread;
}

} // namespace HTTPServers