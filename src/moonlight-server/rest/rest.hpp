#pragma once

#include <boost/asio/ssl.hpp>
#include <helpers/logger.hpp>
#include <moonlight/protocol.hpp>
#include <server_http.hpp>
#include <state/data-structures.hpp>
#include <thread>

namespace SimpleWeb {

using HTTPS = asio::ssl::stream<asio::ip::tcp::socket>;

/**
 * Based on server_https.hpp with the following changes:
 *
 * - SSL handshake will only fail if the client doesn't provide any certificate
 *   - We can't just verify the client cert in the `set_verify_callback`
 *     because we have to respond with an appropriate error message to Moonlight
 *
 * - Returns the x509 client certificate associated with the request socket,
 *   this is needed since we are going to do client certificate validation in each resource endpoint
 *
 *   Unfortunately I couldn't find a better place to add this globally
 *   while still be able to send back a reply in case of validation error
 */
template <> class Server<HTTPS> : public ServerBase<HTTPS> {
  bool set_session_id_context = false;

protected:
  asio::ssl::context context;
  void after_bind() override;
  void accept() override;

public:
  Server(const std::string &certification_file, const std::string &private_key_file);
  static x509::x509_ptr get_client_cert(const std::shared_ptr<typename ServerBase<HTTPS>::Request> &request);
};
} // namespace SimpleWeb

using HttpsServer = SimpleWeb::Server<SimpleWeb::HTTPS>;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using XML = moonlight::XML;

namespace HTTPServers {

void startServer(HttpServer *server, const immer::box<state::AppState> state, int port);

void startServer(HttpsServer *server, const immer::box<state::AppState> state, int port);
} // namespace HTTPServers