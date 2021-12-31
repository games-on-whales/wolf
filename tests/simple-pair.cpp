#include <moonlight/user-pair.hpp>
#include <boost/property_tree/ptree.hpp>

namespace pt = boost::property_tree;

/**
 * @brief A simple (and very unsecure) pair implementation
 * 
 */
class SimplePair : public UserPair {
public:
  SimplePair() {
  }

  bool isPaired(std::string clientID) {
    return _clients.find(clientID) != _clients.not_found();
  };

  bool pair(std::string clientID, std::string pin, std::string clientCert, std::string clientSalt) {
    _clients.put(clientID, pin);
    return true;
  };

private:
  pt::ptree _clients;
};