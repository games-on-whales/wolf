#pragma once
#include <helpers/config.hpp>

#include <openssl/x509.h>

#include <moonlight/data-structures.hpp>
#include <moonlight/user-pair.hpp>

#include <boost/property_tree/ptree.hpp>
namespace pt = boost::property_tree;

namespace moonlight {

constexpr auto M_VERSION = "7.1.431.0";
constexpr auto M_GFE_VERSION = "3.23.0.74";

/**
 * @brief Step 1: GET server informations
 *
 * @param configs
 * @param pair_handler
 * @param isServerBusy
 * @param current_appid
 * @param display_modes
 * @param clientID
 * @return pt::ptree
 */
pt::ptree serverinfo(const Config &config,
                     const UserPair &pair_handler,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> display_modes,
                     const std::string clientID);

/**
 * @brief Pair, phase 1:
 * Moonlight will send a salt and client certificate
 * (PIN + SALT) --> SHA256 needs to be stored in order to use AES to aes_decrypt_cbc the next phases
 * At this stage we only have to send back our public certificate.
 *
 * @return std::pair<pt::ptree, string> the response and the AES key to be used in the next steps
 */
std::pair<pt::ptree, std::string>
pair_get_server_cert(const std::string user_pin, const std::string salt, const X509 &server_cert);

/**
 * @brief Pair, phase 2
 * Using the AES key that we generated in the phase 1 we have to aes_decrypt_cbc the clientchallenge,
 * The response will AES aes_encrypt_cbc:
 *   - the decrypted clientchallenge,
 *   - the signature of our X509 serverCert
 *   - a server secret that we generate (in order to be checked later on)
 *
 * @return std::pair<pt::ptree, std::string> the response and the generated server_secret
 */
std::pair<pt::ptree, std::string>
pair_send_server_challenge(const std::string aes_key, const std::string client_challenge, const X509 &server_cert);

/**
 * @brief Pair, phase 3:
 * Moonlight will send back a serverchallengeresp: an AES encrypted clienthash,
 * we have to send back the pairing secret: using our private key we have to sign the server_secret
 *
 * @return std::pair<pt::ptree, std::string> the response and the decrypted client_hash
 */
std::pair<pt::ptree, std::string> pair_get_client_hash(const std::string aes_key,
                                                       const std::string server_secret,
                                                       const std::string server_challenge_resp,
                                                       const X509 &server_cert);

/**
 * @brief Pair, phase 4 (final)
 * We now have to use everything we exchanged before in order to verify and finally pair the clients
 * We can finally check the client_hash obtained at phase 3 it should contain the following:
 *   - The original server_secret
 *   - The signature of the X509 client_cert
 *   - The unencrypted client_pairing_secret
 * Then using the client_cert we should be able to verify that client_pairing_secret has been signed by Moonlight
 *
 * @return pt::ptree if all checks are fine it'll send paired = 1 else will send back paired = 0
 */
pt::ptree pair_client_pair(const std::string aes_key,
                           const std::string server_secret,
                           const std::string client_hash,
                           const std::string client_pairing_secret,
                           const X509 &client_cert);

} // namespace moonlight