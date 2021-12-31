#pragma once

#include <string>

/**
 * @brief Interface for a class that handles the user pairing protocol
 * 
 */
class UserPair {
public:
  UserPair() {
  }

  virtual bool isPaired(std::string clientID) = 0;  
  virtual bool pair(std::string clientID, std::string pin, std::string clientCert, std::string clientSalt) = 0; // TODO
};