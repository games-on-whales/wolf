#pragma once

#include <boost/property_tree/ptree.hpp>
#define BOOST_BIND_GLOBAL_PLACEHOLDERS // can be removed once this is fixed:
                                       // https://github.com/boostorg/property_tree/pull/50
#include <boost/property_tree/json_parser.hpp>
#undef BOOST_BIND_GLOBAL_PLACEHOLDERS

namespace pt = boost::property_tree;

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <moonlight/data-structures.hpp>
#include <sstream>

namespace moonlight {

/**
 * @brief Configuration class, an abstraction on top of the config.json file
 */
class Config {
public:
  explicit Config(const pt::ptree &state) : _state(state) {
    init_uuid();
  }

  explicit Config(const std::string &config_file) {
    pt::read_json(config_file, _state);
    init_uuid();
  }

  void saveCurrentConfig(const std::string &config_file) const {
    pt::write_json(config_file, _state);
  }

  std::string to_json() {
    return tree_to_json(_state);
  }

  enum VALID_PORTS {
    HTTPS_PORT = -5,
    HTTP_PORT = 0,
    VIDEO_STREAM_PORT = 9,
    CONTROL_PORT = 10,
    AUDIO_STREAM_PORT = 11,
    RTSP_SETUP_PORT = 21
  };

  std::string hostname() const {
    return _state.get<std::string>("hostname", "wolf");
  }

  std::string get_uuid() const {
    return _state.get<std::string>("uid");
  }

  /**
   * @brief All ports are derived from the `base_port` that is configured on startup
   *
   * @param port: one of the VALID_PORTS
   * @return std::uint16_t: a valid port number
   */
  std::uint16_t map_port(VALID_PORTS port) const {
    auto base_port = _state.get<int>("base_port", 47989);
    return (uint16_t)(base_port + port);
  }

  std::string external_ip() const {
    return _state.get<std::string>("external_ip", "1.1.1.1");
  }

  std::string local_ip() const {
    return _state.get<std::string>("local_ip", "127.0.0.1");
  }

  std::string mac_address() const {
    return _state.get<std::string>("mac_address", "AA:BB:CC:DD");
  }

  /////////////////////////////////////////////
  // Pair methods
  ////////

  std::vector<PairedClient> get_paired_clients() const {
    std::vector<PairedClient> r;
    auto paired_clients = _state.get_child_optional("paired_clients");
    if (!paired_clients)
      return {};

    for (const pt::ptree::value_type &item : paired_clients.get())
      r.push_back({
          item.second.get<std::string>("client_id"),
          item.second.get<std::string>("client_cert"),
      });
    return r;
  }

  /**
   * @return If the given clientID is found, will return the client certificate associated with it
   */
  std::optional<std::string> get_client_cert(const std::string &clientID) const {
    auto paired_clients = get_paired_clients();
    auto search_result =
        std::find_if(paired_clients.begin(), paired_clients.end(), [clientID](PairedClient &pair_client) {
          return pair_client.client_id == clientID;
        });
    if (search_result != paired_clients.end())
      return search_result->client_cert;
    else
      return std::nullopt;
  }

  bool isPaired(const std::string &clientID) const {
    return get_client_cert(clientID) != std::nullopt;
  }

  /**
   * Side effect, will add clientID and clientCert to the local state config
   */
  void pair(const std::string &clientID, const std::string &clientCert) {
    pt::ptree client_info;
    client_info.put("client_id", clientID);
    client_info.put("client_cert", clientCert);

    auto paired_clients = _state.get_child_optional("paired_clients");
    if (!paired_clients) { // If it's not present already, it needs to be added
      auto new_paired_clients = pt::ptree();
      new_paired_clients.push_back(pt::ptree::value_type("", client_info));
      _state.add_child("paired_clients", new_paired_clients);
    } else { // updating the ptree pointer will update it inside _state
      paired_clients->push_back(pt::ptree::value_type("", client_info));
    }
  }

  /////////////////////////////////////////////
  // Apps
  ////////

  std::vector<App> get_apps() const {
    std::vector<App> r;
    auto apps = _state.get_child_optional("apps");
    if (!apps)
      return {};

    for (const pt::ptree::value_type &item : apps.get())
      r.push_back({item.second.get<std::string>("title"),
                   item.second.get<std::string>("id"),
                   item.second.get<bool>("support_hdr")});
    return r;
  }

private:
  pt::ptree _state;

  /**
   * @brief: we need a stable server UUID otherwise we'll generate a new on on each new request
   */
  void init_uuid() {
    auto saved_uuid = _state.get_optional<std::string>("uid");
    if (!saved_uuid) {
      auto new_uuid = gen_uuid();
      _state.put("uid", new_uuid);
    }
  }

  static std::string gen_uuid() {
    auto uuid = boost::uuids::random_generator()();
    return boost::lexical_cast<std::string>(uuid);
  }

  static std::string tree_to_json(const pt::ptree &pt) {
    std::stringstream ss;
    boost::property_tree::json_parser::write_json(ss, pt);
    return ss.str();
  }
};
} // namespace moonlight