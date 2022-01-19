#pragma once

#include <string>

namespace moonlight {

/**
 * @brief Interface for a class that handles the user pairing protocol
 *
 */
class UserPair {
public:
  UserPair() {
  }

  virtual bool isPaired(const std::string clientID) const = 0;
  virtual bool pair(const std::string clientID,
                    const std::string pin,
                    const std::string clientCert,
                    const std::string clientSalt) = 0; // TODO
};

}