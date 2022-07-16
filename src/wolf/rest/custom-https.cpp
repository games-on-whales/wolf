#include "state/config.hpp"
#include <crypto/crypto.hpp>
#include <helpers/logger.hpp>
#include <server_https.hpp>

/**
 * Override default SimpleWeb::HTTPS by adding custom SSL cert validation that
 * allows only to already paired Moonlight clients to be able to connect to the SSL endpoints.
 */
class HTTPSCustomCert : public SimpleWeb::Server<SimpleWeb::HTTPS> {
public:
  HTTPSCustomCert(const std::string &certification_file,
                  const std::string &private_key_file,
                  const std::shared_ptr<state::JSONConfig> &config)
      : SimpleWeb::Server<SimpleWeb::HTTPS>(certification_file, private_key_file, std::string()) {
    context.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert |
                            boost::asio::ssl::verify_client_once);

    context.set_verify_callback([config](bool pre_verified, boost::asio::ssl::verify_context &ctx) -> int {
      auto x509 = X509_STORE_CTX_get_current_cert(ctx.native_handle());
      if (!x509) {
        logs::log(logs::error, "Missing certificate on HTTPS server, closing connection");
        return 0;
      }

      auto clients = config->get_paired_clients();
      for (const auto &client : clients) {
        auto paired_cert = x509::cert_from_string(client.client_cert);
        auto valid_error = x509::verification_error(paired_cert, x509);
        if (valid_error) {
          logs::log(logs::warning, "SSL certification validation error: {}", valid_error.value());
        } else { // validation successful!
          return true;
        }
      }
      logs::log(logs::warning, "Received HTTPS request from a client which wasn't previously paired.");
      return false;
    });
  }
};