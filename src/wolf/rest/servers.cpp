#include <boost/property_tree/json_parser.hpp>
#include <immer/atom.hpp>
#include <rest/endpoints.hpp>
#include <rest/helpers.hpp>
#include <rest/rest.hpp>
#include <state/config.hpp>

namespace HTTPServers {

/**
 * A bit of magic here, it'll load up the pin.html via Cmake (look for `make_includable`)
 */
constexpr char const *pin_html =
#include "html/pin.include.html"
    ;

namespace bt = boost::property_tree;

/**
 * @brief Start the generic server on the specified port
 * @return std::thread: the thread where this server will run
 */
std::thread startServer(HttpServer *server, const std::shared_ptr<state::AppState> &state, int port) {
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

  auto pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<state::PairSignal>>>>();

  server->resource["^/pin/$"]["GET"] = [](auto resp, auto req) { resp->write(pin_html); };
  server->resource["^/pin/$"]["POST"] = [pairing_atom](auto resp, auto req) {
    try {
      bt::ptree pt;

      read_json(req->content, pt);

      auto pin = pt.get<std::string>("pin");
      auto secret = pt.get<std::string>("secret");
      logs::log(logs::debug, "Received POST /pin/ pin:{} secret:{}", pin, secret);

      auto pair_request = pairing_atom->load()->at(secret);
      pair_request->user_pin->set_value(pin);
      resp->write("OK");
      pairing_atom->update([&secret](auto m) { return m.erase(secret); });
    } catch (const std::exception &e) {
      *resp << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
    }
  };

  server->resource["^/unpair$"]["GET"] = [&state](auto resp, auto req) {
    SimpleWeb::CaseInsensitiveMultimap headers = req->parse_query_string();
    auto client_id = get_header(headers, "uniqueid");
    auto client_ip = req->remote_endpoint().address().to_string();
    auto cache_key = client_id.value() + "@" + client_ip;

    logs::log(logs::info, "Unpairing: {}", cache_key);
    state::unpair(state->config, state->pairing_cache->load()->at(cache_key));

    XML xml;
    xml.put("root.<xmlattr>.status_code", 200);
    send_xml<SimpleWeb::HTTP>(resp, SimpleWeb::StatusCode::success_ok, xml);
  };

  std::thread server_thread(
      [pairing_atom, event_bus = state->event_bus](auto server) {
        auto pair_handler = event_bus->register_handler<immer::box<state::PairSignal>>(
            [pairing_atom](const immer::box<state::PairSignal> &pair_sig) {
              pairing_atom->update([&pair_sig](auto m) {
                auto secret = crypto::str_to_hex(crypto::random(8));
                logs::log(logs::info, "Insert pin at http://localhost:47989/pin/#{}", secret);
                return m.set(secret, pair_sig);
              });
            });

        // Start server
        server->start([](unsigned short port) { logs::log(logs::info, "HTTP server listening on port: {} ", port); });

        pair_handler.unregister();
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

std::thread startServer(HttpsServer *server, const std::shared_ptr<state::AppState> &state, int port) {
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