#pragma once

#include <future>
#include <rest/custom-https.cpp>
#include <rest/endpoints.cpp>
#include <server_http.hpp>

using HttpsServer = SimpleWeb::Server<SimpleWeb::HTTPS>;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

namespace HTTPServers {

/**
 * @brief Start the generic server on the specified port
 * @return std::thread: the thread where this server will run
 */
std::thread
startServer(SimpleWeb::Server<SimpleWeb::HTTP> *server, const std::shared_ptr<state::AppState> &state, int port) {
  server->config.port = port;
  server->config.address = "0.0.0.0";
  server->default_resource["GET"] = endpoints::not_found<SimpleWeb::HTTP>;
  server->default_resource["POST"] = endpoints::not_found<SimpleWeb::HTTP>;

  server->resource["^/serverinfo$"]["GET"] = [&state](auto resp, auto req) {
    endpoints::serverinfo<SimpleWeb::HTTP>(resp, req, state);
  };

  server->resource["^/pair$"]["GET"] = [&state](auto resp, auto req) {
    endpoints::pair<SimpleWeb::HTTP>(resp, req, state);
  };

  server->resource["^/unpair$"]["GET"] = [&state](auto resp, auto req) {
    SimpleWeb::CaseInsensitiveMultimap headers = req->parse_query_string();
    auto client_id = get_header(headers, "uniqueid");
    auto client_ip = req->remote_endpoint().address().to_string();
    auto cache_key = client_id.value() + "@" + client_ip;

    state->pairing_cache.update([&cache_key](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.erase(cache_key);
    });

    XML xml;
    xml.put("root.<xmlattr>.status_code", 200);
    send_xml<SimpleWeb::HTTP>(resp, SimpleWeb::StatusCode::success_ok, xml);
  };

  std::thread server_thread(
      [](auto server) {
        // Start server
        server->start([](unsigned short port) { logs::log(logs::info, "HTTP server listening on port: {} ", port); });
      },
      server);

  return server_thread;
}

std::optional<state::PairedClient>
get_client_if_paired(const std::shared_ptr<state::AppState> &state,
                     const std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request) {
  auto client_cert = SimpleWeb::Server<SimpleWeb::HTTPS>::get_client_cert(request);
  return state::get_client_via_ssl(state->config, client_cert);
}

void reply_unauthorized(const std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request,
                        const std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response) {
  logs::log(logs::warning, "Received HTTPS request from a client which wasn't previously paired.");

  XML xml;

  xml.put("root.<xmlattr>.status_code"s, 401);
  xml.put("root.<xmlattr>.query"s, request->path);
  xml.put("root.<xmlattr>.status_message"s, "The client is not authorized. Certificate verification failed."s);

  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::client_error_unauthorized, xml);
}

std::thread
startServer(SimpleWeb::Server<SimpleWeb::HTTPS> *server, const std::shared_ptr<state::AppState> &state, int port) {
  server->config.port = port;
  server->config.address = "0.0.0.0";
  server->default_resource["GET"] = endpoints::not_found<SimpleWeb::HTTPS>;
  server->default_resource["POST"] = endpoints::not_found<SimpleWeb::HTTPS>;

  server->resource["^/serverinfo$"]["GET"] = [&state](auto resp, auto req) {
    if (get_client_if_paired(state, req)) {
      endpoints::serverinfo<SimpleWeb::HTTPS>(resp, req, state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  server->resource["^/pair$"]["GET"] = [&state](auto resp, auto req) {
    if (get_client_if_paired(state, req)) {
      endpoints::pair<SimpleWeb::HTTPS>(resp, req, state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  server->resource["^/applist$"]["GET"] = [&state](auto resp, auto req) {
    if (get_client_if_paired(state, req)) {
      endpoints::https::applist<SimpleWeb::HTTPS>(resp, req, state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  server->resource["^/launch"]["GET"] = [&state](auto resp, auto req) {
    if (auto client = get_client_if_paired(state, req)) {
      endpoints::https::launch<SimpleWeb::HTTPS>(resp, req, client.value(), state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  // TODO: add missing
  // https_server.resource["^/appasset$"]["GET"]
  // https_server.resource["^/resume$"]["GET"]
  // https_server.resource["^/cancel$"]["GET"]

  std::thread server_thread(
      [](auto server) {
        // Start server
        server->start([](unsigned short port) { logs::log(logs::info, "HTTPS server listening on port: {} ", port); });
      },
      server);

  return server_thread;
}

} // namespace HTTPServers