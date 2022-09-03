#include <boost/property_tree/ptree.hpp>
#define BOOST_BIND_GLOBAL_PLACEHOLDERS // can be removed once this is fixed:
                                       // https://github.com/boostorg/property_tree/pull/50
#include <boost/property_tree/json_parser.hpp>
#undef BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <helpers/logger.hpp>
#include <range/v3/view.hpp>
#include <state/config.hpp>
#include <state/data-structures.hpp>

namespace pt = boost::property_tree;
using Json = pt::ptree;
using namespace ranges;

namespace state {

bool file_exist(const std::string &filename) {
  std::fstream fs(filename);
  return fs.good();
}

static std::string gen_uuid() {
  auto uuid = boost::uuids::random_generator()();
  return boost::lexical_cast<std::string>(uuid);
}

std::string init_uuid(const Json &cfg) {
  auto saved_uuid = cfg.get_optional<std::string>("uuid");
  if (!saved_uuid) {
    return gen_uuid();
  }
  return saved_uuid.get();
}

state::PairedClientList get_paired_clients(const Json &cfg) {
  auto paired_clients = cfg.get_child_optional("paired_clients");
  if (!paired_clients)
    return {};

  return paired_clients.get()                                                                              //
         | views::transform([](const pt::ptree::value_type &item) {                                        //
             return PairedClient{item.second.get<std::string>("client_id"),                                //
                                 item.second.get<std::string>("client_cert"),                              //
                                 item.second.get<unsigned short>("rtsp_port", state::RTSP_SETUP_PORT),     //
                                 item.second.get<unsigned short>("control_port", state::CONTROL_PORT),     //
                                 item.second.get<unsigned short>("video_port", state::VIDEO_STREAM_PORT),  //
                                 item.second.get<unsigned short>("audio_port", state::AUDIO_STREAM_PORT)}; //
           })                                                                                              //
         | to<state::PairedClientList>();
}

immer::vector<moonlight::App> get_apps(const Json &cfg) {
  auto paired_clients = cfg.get_child_optional("apps");
  if (!paired_clients)
    return {};

  return paired_clients.get()                                             //
         | views::transform([](const pt::ptree::value_type &item) {       //
             return moonlight::App{item.second.get<std::string>("title"), //
                                   item.second.get<std::string>("id"),    //
                                   item.second.get<bool>("support_hdr")}; //
           })                                                             //
         | to<immer::vector<moonlight::App>>();                           //
}

template <class S> Config load_or_default(const S &source) {
  Json json;
  if (file_exist(source)) {
    pt::read_json(source, json);
    auto clients = get_paired_clients(json);
    auto atom = new immer::atom<state::PairedClientList>(clients);
    return {init_uuid(json), json.get<std::string>("hostname", "wolf"), *atom, get_apps(json)};
  } else {
    logs::log(logs::warning, "Unable to open config file: {}, using defaults", source);
    state::PairedClientList clients = {};
    auto atom = new immer::atom<state::PairedClientList>(clients);
    return {gen_uuid(), "wolf", *atom, {{"Desktop", "1", true}}};
  }
}

template <class S> void save(const Config &cfg, const S &dest) {
  Json json;
  json.put("uuid", cfg.uuid);
  json.put("hostname", cfg.hostname);

  Json p_clients;
  auto clients = cfg.paired_clients.load().get();
  for (const auto &client : clients) {
    Json client_json;
    client_json.put("client_id", client->client_id);
    client_json.put("client_cert", client->client_cert);
    client_json.put("rtsp_port", client->rtsp_port);
    client_json.put("control_port", client->control_port);
    client_json.put("video_port", client->video_port);
    client_json.put("audio_port", client->audio_port);
    p_clients.push_back(Json::value_type("", client_json));
  }
  json.put_child("paired_clients", p_clients);

  Json apps;
  for (const auto &app : cfg.apps) {
    Json app_json;
    app_json.put("title", app.title);
    app_json.put("id", app.id);
    app_json.put("support_hdr", app.support_hdr);
    apps.push_back(Json::value_type("", app_json));
  }
  json.put_child("apps", apps);

  pt::write_json(dest, json);
}

std::optional<PairedClient> get_client_via_ssl(const Config &cfg, x509_st *client_cert) {
  auto paired_clients = cfg.paired_clients.load();
  auto search_result =
      std::find_if(paired_clients->begin(), paired_clients->end(), [&client_cert](const PairedClient &pair_client) {
        auto paired_cert = x509::cert_from_string(pair_client.client_cert);
        auto verification_error = x509::verification_error(paired_cert, client_cert);
        if (verification_error) {
          logs::log(logs::trace, "X509 certificate verification error: {}", verification_error.value());
          return false;
        } else {
          return true;
        }
      });
  if (search_result != paired_clients->end()) {
    return *search_result;
  } else {
    return std::nullopt;
  }
}

void pair(const Config &cfg, const PairedClient &client) {
  cfg.paired_clients.update(
      [&client](const state::PairedClientList &paired_clients) { return paired_clients.push_back(client); });
}

void unpair(const Config &cfg, const PairedClient &client) {
  cfg.paired_clients.update([&client](const state::PairedClientList &paired_clients) {
    return paired_clients                                               //
           | views::filter([&client](auto paired_client) {              //
               return paired_client->client_cert != client.client_cert; //
             })                                                         //
           | to<state::PairedClientList>();                             //
  });
}

/**
 * Turns the json object into a string.
 * Useful for debugging
 */
std::string to_str(const Json &pt) {
  std::stringstream ss;
  pt::json_parser::write_json(ss, pt);
  return ss.str();
}
} // namespace state