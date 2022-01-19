#pragma once

#include <boost/property_tree/ptree.hpp>
#include <moonlight/user-pair.hpp>

namespace pt = boost::property_tree;

/**
 * @brief A simple (and very unsecure) pair implementation
 *
 */
class SimplePair : public moonlight::UserPair {
public:
  SimplePair() {
  }

  bool isPaired(const std::string clientID) const {
    return _clients.find(clientID) != _clients.not_found();
  };

  bool
  pair(const std::string clientID, const std::string pin, const std::string clientCert, const std::string clientSalt) {
    _clients.put(clientID, pin);
    return true;
  };

private:
  pt::ptree _clients;
};