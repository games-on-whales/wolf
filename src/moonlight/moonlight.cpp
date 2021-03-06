#include <crypto/crypto.hpp>
#include <moonlight/protocol.hpp>

namespace pt = boost::property_tree;

namespace moonlight {

pt::ptree serverinfo(const Config &config,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> &display_modes,
                     const std::string &clientID) {
  int pair_status = config.isPaired(clientID);
  pt::ptree resp;

  resp.put("root.<xmlattr>.status_code", 200);
  resp.put("root.hostname", config.hostname());

  resp.put("root.appversion", M_VERSION);
  resp.put("root.GfeVersion", M_GFE_VERSION);
  resp.put("root.uniqueid", config.get_uuid());

  resp.put("root.MaxLumaPixelsHEVC", "0");
  // TODO: resp.put("root.MaxLumaPixelsHEVC",config::video.hevc_mode > 1 ? "1869449984" : "0");
  resp.put("root.ServerCodecModeSupport", "3"); // TODO: what are the modes here?

  resp.put("root.HttpsPort", config.map_port(config.HTTPS_PORT));
  resp.put("root.ExternalPort", config.map_port(config.HTTP_PORT));
  resp.put("root.mac", config.mac_address());
  resp.put("root.ExternalIP", config.external_ip());
  resp.put("root.LocalIP", config.local_ip());

  pt::ptree display_nodes;
  for (auto mode : display_modes) {
    pt::ptree display_node;
    display_node.put("Width", mode.width);
    display_node.put("Height", mode.height);
    display_node.put("RefreshRate", mode.refreshRate);

    display_nodes.add_child("DisplayMode", display_node);
  }

  resp.add_child("root.SupportedDisplayMode", display_nodes);
  resp.put("root.PairStatus", pair_status);
  resp.put("root.currentgame", current_appid);
  resp.put("root.state", isServerBusy ? "SUNSHINE_SERVER_BUSY" : "SUNSHINE_SERVER_FREE");
  return resp;
}

namespace pair {
std::pair<pt::ptree, std::string>
get_server_cert(const std::string &user_pin, const std::string &salt, const std::string &server_cert_pem) {
  pt::ptree resp;

  auto key = gen_aes_key(salt, user_pin);
  auto cert_hex = crypto::str_to_hex(server_cert_pem);

  resp.put("root.paired", 1);
  resp.put("root.plaincert", cert_hex);
  resp.put("root.<xmlattr>.status_code", 200);

  return std::make_pair(resp, key);
}

std::string gen_aes_key(const std::string &salt, const std::string &pin) {
  auto salt_parsed = crypto::hex_to_str(salt, true);
  auto aes_key = crypto::hex_to_str(crypto::sha256(salt_parsed + pin), true);
  aes_key.resize(16);
  return aes_key;
}

std::pair<pt::ptree, std::pair<std::string, std::string>>
send_server_challenge(const std::string &aes_key,
                      const std::string &client_challenge,
                      const std::string &server_cert_signature,
                      const std::string &server_secret,
                      const std::string &server_challenge) {
  pt::ptree resp;

  auto client_challenge_hex = crypto::hex_to_str(client_challenge, true);
  auto decrypted_challenge = crypto::aes_decrypt_ecb(client_challenge_hex, aes_key);
  auto hash = crypto::hex_to_str(crypto::sha256(decrypted_challenge + server_cert_signature + server_secret), true);
  auto plain_text = hash + server_challenge;
  auto encrypted = crypto::aes_encrypt_ecb(plain_text, aes_key, crypto::random(AES_BLOCK_SIZE), false);

  resp.put("root.paired", 1);
  resp.put("root.challengeresponse", crypto::str_to_hex(encrypted));
  resp.put("root.<xmlattr>.status_code", 200);

  return std::make_pair(resp, std::make_pair(server_secret, server_challenge));
}

std::pair<pt::ptree, std::string> get_client_hash(const std::string &aes_key,
                                                  const std::string &server_secret,
                                                  const std::string &server_challenge_resp,
                                                  const std::string &server_cert_private_key) {
  pt::ptree resp;

  auto server_challenge_hex = crypto::hex_to_str(server_challenge_resp, true);
  auto decrypted_challenge = crypto::aes_decrypt_ecb(server_challenge_hex, aes_key);
  auto signature = crypto::sign(server_secret, server_cert_private_key);

  resp.put("root.pairingsecret", crypto::str_to_hex(server_secret + signature));
  resp.put("root.paired", 1);
  resp.put("root.<xmlattr>.status_code", 200);

  return std::make_pair(resp, decrypted_challenge);
}

pt::ptree client_pair(const std::string &aes_key,
                      const std::string &server_challenge,
                      const std::string &client_hash,
                      const std::string &client_pairing_secret,
                      const std::string &client_public_cert_signature,
                      const std::string &client_cert_public_key) {
  pt::ptree resp;
  resp.put("root.<xmlattr>.status_code", 200);
  auto digest_size = 256;

  auto pairing_secret = crypto::hex_to_str(client_pairing_secret, true);
  auto client_secret = pairing_secret.substr(0, 16);
  auto client_signature = pairing_secret.substr(16, digest_size);

  auto hash = crypto::hex_to_str(crypto::sha256(server_challenge + client_public_cert_signature + client_secret), true);
  if (hash != client_hash) {
    resp.put("root.paired", 0);
    return resp;
  }

  if (crypto::verify(client_secret, client_signature, client_cert_public_key)) {
    resp.put("root.paired", 1);
  } else {
    resp.put("root.paired", 0);
  }
  return resp;
}
} // namespace pair
} // namespace moonlight