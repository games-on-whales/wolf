#pragma once

#include <future>
#include <memory>
#include <utility>

#include <x509.cpp>

#include <data-structures.hpp>
#include <endpoints.cpp>
#include <helpers.cpp>
#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>

using HttpsServer = SimpleWeb::Server<SimpleWeb::HTTPS>;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

namespace HTTPServers {

/**
 * @brief Create an HTTPS server
 *
 * @param pkey_filename
 * @param cert_filename
 * @return std::unique_ptr<HttpsServer>
 */
std::unique_ptr<HttpsServer> createHTTPS(const std::string &pkey_filename, const std::string &cert_filename) {
  if (!x509::cert_exists(pkey_filename, cert_filename)) {
    logs::log(logs::info, "x509 certificates not present, generating...");
    auto pkey = x509::generate_key();
    auto cert = x509::generate_x509(pkey);
    x509::write_to_disk(pkey, pkey_filename, cert, cert_filename);
    x509::cleanup(pkey, cert);
  }

  return std::make_unique<HttpsServer>(cert_filename, pkey_filename);
}

/**
 * @brief Create an HTTP server
 *
 * @return std::unique_ptr<HttpServer>
 */
std::unique_ptr<HttpServer> createHTTP() {
  return std::make_unique<HttpServer>();
}

/**
 * @brief Start the generic server on the specified port
 * @return std::thread: the thread where this server will run
 */
template <typename T> std::thread startServer(SimpleWeb::Server<T> *server, const LocalState &state, int port) {
  server->config.port = port;
  server->config.address = {"0.0.0.0"};
  server->default_resource["GET"] = endpoints::not_found<T>;
  server->default_resource["POST"] = endpoints::not_found<T>;

  // Moonlight protocol
  server->resource["^/serverinfo$"]["GET"] = [&state](auto resp, auto req) {
    endpoints::serverinfo<T>(resp, req, state);
  };

  server->resource["^/pair$"]["GET"] = [&state](auto resp, auto req) { endpoints::pair<T>(resp, req, state); };

  std::promise<unsigned short> server_port;
  std::thread server_thread([&server, &server_port]() {
    // Start server
    server->start([&server_port](unsigned short port) { server_port.set_value(port); });
  });
  logs::log(logs::debug, "{} server listening on port: {} ", tunnel<T>::to_string, server_port.get_future().get());

  return server_thread;
}

} // namespace HTTPServers