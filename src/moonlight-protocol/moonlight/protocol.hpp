#pragma once
#include "data-structures.hpp"
#include <boost/property_tree/ptree.hpp>
#include <crypto/crypto.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <immer/vector.hpp>

namespace moonlight {

namespace pt = boost::property_tree;
using XML = pt::ptree;

constexpr auto M_VERSION = "7.1.431.-1";
constexpr auto M_GFE_VERSION = "3.23.0.74";

/**
 * @brief Step 1: GET server status
 *
 * @param isServerBusy: true if we are already running a streaming session
 * @param current_appid: -1 if no app is running, otherwise the ID as defined in the app list
 * @param uuid
 * @param hostname
 * @param https_port
 * @param http_port
 * @param mac_address
 * @param external_ip
 * @param local_ip
 * @param display_modes
 * @param pair_status: true if the client is already paired
 * @return ptree the XML response to be sent
 */
XML serverinfo(bool isServerBusy,
               int current_appid,
               int https_port,
               int http_port,
               const std::string &uuid,
               const std::string &hostname,
               const std::string &mac_address,
               const std::string &local_ip,
               const immer::array<DisplayMode> &display_modes,
               int pair_status,
               bool support_hevc,
               bool support_av1);

/**
 * @brief Step 2: PAIR a new client
 *
 * Defines the Moonlight client/server pairing protocol
 */
namespace pair {

/**
 * @brief Pair, phase 1:
 *
 * Moonlight will send a salt and client certificate, we'll also need the user provided pin.
 *
 * PIN and SALT will be used to derive a shared AES key that needs to be stored
 * in order to be used to decrypt_symmetric in the next phases (see `gen_aes_key`).
 *
 * At this stage we only have to send back our public certificate (`plaincert`).
 *
 * @return pair<ptree, string> the XML response and the AES key to be used in the next steps
 */
std::pair<XML, std::string>
get_server_cert(const std::string &user_pin, const std::string &salt, const std::string &server_cert_pem);

/**
 * @brief will derive a common AES key given the salt and the user provided pin
 *
 * This method needs to match what Moonlight is doing internally otherwise we wouldn't be able to decrypt_symmetric
 * the client challenge.
 *
 * @return `SHA256(SALT + PIN)[0:16]`
 */
std::string gen_aes_key(const std::string &salt, const std::string &pin);

/**
 * @brief Pair, phase 2
 *
 * Using the AES key that we generated in the phase 1 we have to decrypt the client challenge,
 *
 * We generate a SHA256 hash with the following:
 *  - Decrypted challenge
 *  - Server certificate signature
 *  - Server secret: a randomly generated secret
 *
 * The hash + server_challenge will then be AES encrypted and sent as the `challengeresponse` in the returned XML
 *
 * @return pair<ptree, pair<string, string>> the response and the pair of generated:
 * server_secret, server_challenge
 */
std::pair<XML, std::pair<std::string, std::string>>
send_server_challenge(const std::string &aes_key,
                      const std::string &client_challenge,
                      const std::string &server_cert_signature,
                      const std::string &server_secret = crypto::random(16),
                      const std::string &server_challenge = crypto::random(16));

/**
 * @brief Pair, phase 3
 *
 * Moonlight will send back a `serverchallengeresp`: an AES encrypted client hash,
 * we have to send back the `pairingsecret`:
 * using our private key we have to sign the certificate_signature + server_secret (generated in phase 2)
 *
 * @return pair<ptree, string> the response and the decrypted client_hash
 */
std::pair<XML, std::string> get_client_hash(const std::string &aes_key,
                                            const std::string &server_secret,
                                            const std::string &server_challenge_resp,
                                            const std::string &server_cert_private_key);

/**
 * @brief Pair, phase 4 (final)
 *
 * We now have to use everything we exchanged before in order to verify and finally pair the clients
 *
 * We'll check the client_hash obtained at phase 3, it should contain the following:
 *   - The original server_challenge
 *   - The signature of the X509 client_cert
 *   - The unencrypted client_pairing_secret
 * We'll check that SHA256(server_challenge + client_public_cert_signature + client_secret) == client_hash
 *
 * Then using the client certificate public key we should be able to verify that
 * the client secret has been signed by Moonlight
 *
 * @return ptree: The XML response will contain:
 * paired = 1, if all checks are fine
 * paired = 0, otherwise
 */
XML client_pair(const std::string &aes_key,
                const std::string &server_challenge,
                const std::string &client_hash,
                const std::string &client_pairing_secret,
                const std::string &client_public_cert_signature,
                const std::string &client_cert_public_key);
} // namespace pair

/**
 * After pairing and selecting the host Moonlight will show a list of applications that can be started,
 * here we just return a list of the names.
 *
 * @param apps: a list of available apps
 * @return ptree: The XML response, a list of apps
 */
XML applist(const immer::vector<App> &apps);

/**
 * After the user selects an app to launch we have to negotiate the IP and PORT for the RTSP session
 *
 * @param config: local state
 * @return:
 */
XML launch_success(const std::string &local_ip, const std::string &rtsp_port);
// TODO: launch_error()

XML launch_resume(const std::string &local_ip, const std::string &rtsp_port);
} // namespace moonlight
