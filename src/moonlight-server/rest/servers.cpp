#include <boost/property_tree/json_parser.hpp>
#include <events/events.hpp>
#include <immer/atom.hpp>
#include <rest/endpoints.hpp>

namespace HTTPServers {

/**
 * A bit of magic here, it'll load up the pin.html via Cmake (look for `make_includable`)
 */
constexpr char const *pin_html =
#include "html/pin.include.html"
    ;

namespace bt = boost::property_tree;
using namespace wolf::core;

/**
 * @brief Start the generic server on the specified port
 * @return std::thread: the thread where this server will run
 */
void startServer(HttpServer *server, const immer::box<state::AppState> state, int port) {
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

  auto pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>();

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
    auto client = state->pairing_cache->load()->at(cache_key);
    state::unpair(state->config, state::PairedClient{.client_cert = client.client_cert});

    XML xml;
    xml.put("root.<xmlattr>.status_code", 200);
    send_xml<SimpleWeb::HTTP>(resp, SimpleWeb::StatusCode::success_ok, xml);
  };

  auto pair_handler = state->event_bus->register_handler<immer::box<events::PairSignal>>(
      [pairing_atom](const immer::box<events::PairSignal> pair_sig) {
        pairing_atom->update([&pair_sig](auto m) {
          auto secret = crypto::str_to_hex(crypto::random(8));
          logs::log(logs::info, "Insert pin at http://{}:47989/pin/#{}", pair_sig->host_ip, secret);
          return m.set(secret, pair_sig);
        });
      });

  // Start server
  server->start([](unsigned short port) { logs::log(logs::info, "HTTP server listening on port: {} ", port); });

  pair_handler.unregister();
}

std::optional<state::PairedClient>
get_client_if_paired(const immer::box<state::AppState> state,
                     const std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request) {
  auto client_cert = SimpleWeb::Server<SimpleWeb::HTTPS>::get_client_cert(request);
  return state::get_client_via_ssl(state->config, std::move(client_cert));
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

void startServer(HttpsServer *server, const immer::box<state::AppState> state, int port) {
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
      endpoints::https::applist(resp, req, state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  server->resource["^/launch"]["GET"] = [&state](auto resp, auto req) {
    if (auto client = get_client_if_paired(state, req)) {
      endpoints::https::launch(resp, req, client.value(), state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  server->resource["^/resume$"]["GET"] = [&state](auto resp, auto req) {
    if (auto client = get_client_if_paired(state, req)) {
      endpoints::https::resume(resp, req, client.value(), state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  server->resource["^/cancel$"]["GET"] = [&state](auto resp, auto req) {
    if (auto client = get_client_if_paired(state, req)) {
      endpoints::https::cancel(resp, req, client.value(), state);
    } else {
      reply_unauthorized(req, resp);
    }
  };

  // TODO: add missing
  // https_server.resource["^/appasset$"]["GET"]

  server->start([](unsigned short port) { logs::log(logs::info, "HTTPS server listening on port: {} ", port); });
}

} // namespace HTTPServers