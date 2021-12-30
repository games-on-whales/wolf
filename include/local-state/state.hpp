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

class LocalState {
public:
  LocalState(pt::ptree state) {
    _state = state;
  }

  LocalState(std::string config_file) {
    pt::read_json(config_file, _state);
  }

  void saveCurrentConfig(std::string config_file) {
    pt::write_json(config_file, _state);
  }

  enum VALID_PORTS
  {
    HTTPS_PORT = -5,
    HTTP_PORT = 0,
  };

  std::string hostname() {
    return _state.get<std::string>("hostname", "wolf");
  }

  std::string get_uuid() {
    return _state.get<std::string>("uid", gen_uuid());
  }

  /**
   * @brief All ports are derived from the `base_port` that is configured on startup
   *
   * @param port: one of the VALID_PORTS
   * @return std::uint16_t: a valid port number
   */
  std::uint16_t map_port(VALID_PORTS port) {
    auto base_port = _state.get<int>("base_port");
    return (std::uint16_t)(base_port + port);
  }

  std::string external_ip() {
    return _state.get<std::string>("external_ip");
  }

  std::string local_ip() {
    return _state.get<std::string>("local_ip");
  }

  std::string mac_address() {
    return _state.get<std::string>("mac_address");
  }

  bool isPaired(std::string clientID) {
    return false; // TODO
  }

private:
  pt::ptree _state;

  std::string gen_uuid() {
    auto uuid = boost::uuids::random_generator()();
    return boost::lexical_cast<std::string>(uuid);
  }
};