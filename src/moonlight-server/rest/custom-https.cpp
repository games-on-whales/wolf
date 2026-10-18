#include <rest/rest.hpp>

namespace SimpleWeb {

Server<HTTPS>::Server(const std::string &certification_file, const std::string &private_key_file)
    : ServerBase<HTTPS>::ServerBase(443), context(asio::ssl::context::tls) {

  context.use_certificate_chain_file(certification_file);
  context.use_private_key_file(private_key_file, asio::ssl::context::pem);

  context.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert |
                          boost::asio::ssl::verify_client_once);
  context.set_verify_callback([](int verified, boost::asio::ssl::verify_context &ctx) {
    // To respond with an error message, a connection must be established
    return 1;
  });

  this->on_error = [](std::shared_ptr<typename ServerBase<HTTPS>::Request> request, const error_code &ec) -> void {
    // TODO: why is stream truncated happening so frequently?
    logs::log(ec.value() == 1 || ec.value() == 167773206 ? logs::trace : logs::warning,
              "HTTPS error during request at {} error code: {} - {}",
              request->path,
              ec.value(),
              ec.message());
    return;
  };
}

void Server<HTTPS>::after_bind() {
  if (set_session_id_context) {
    // Creating session_id_context from address:port but reversed due to small SSL_MAX_SSL_SESSION_ID_LENGTH
    auto session_id_context = std::to_string(acceptor->local_endpoint().port()) + ':';
    session_id_context.append(config.address.rbegin(), config.address.rend());
    SSL_CTX_set_session_id_context(
        context.native_handle(),
        reinterpret_cast<const unsigned char *>(session_id_context.data()),
        static_cast<unsigned int>(std::min<std::size_t>(session_id_context.size(), SSL_MAX_SSL_SESSION_ID_LENGTH)));
  }
}

void Server<HTTPS>::accept() {
  auto connection = create_connection(*io_service, context);

  acceptor->async_accept(connection->socket->lowest_layer(), [this, connection](const error_code &ec) {
    auto lock = connection->handler_runner->continue_lock();
    if (!lock)
      return;

    if (ec != error::operation_aborted)
      this->accept();

    auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);

    if (!ec) {
      asio::ip::tcp::no_delay option(true);
      error_code ec;
      session->connection->socket->lowest_layer().set_option(option, ec);

      session->connection->set_timeout(config.timeout_request);
      session->connection->socket->async_handshake(asio::ssl::stream_base::server,
                                                   [this, session](const error_code &ec) {
                                                     session->connection->cancel_timeout();
                                                     auto lock = session->connection->handler_runner->continue_lock();
                                                     if (!lock)
                                                       return;
                                                     if (!ec)
                                                       this->read(session);
                                                     else if (this->on_error)
                                                       this->on_error(session->request, ec);
                                                   });
    } else if (this->on_error)
      this->on_error(session->request, ec);
  });
}

x509::x509_ptr Server<HTTPS>::get_client_cert(const std::shared_ptr<typename ServerBase<HTTPS>::Request> &request) {
  auto connection = request->connection.lock();
  auto cert = SSL_get_peer_certificate(connection->socket->native_handle());
  return x509::x509_ptr(cert, X509_free);
}

} // namespace SimpleWeb