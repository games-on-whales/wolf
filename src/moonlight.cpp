#include <moonlight/crypto.hpp>
#include <moonlight/protocol.hpp>

namespace pt = boost::property_tree;

namespace moonlight {

pt::ptree serverinfo(const Config &config,
                     const UserPair &pair_handler,
                     bool isServerBusy,
                     int current_appid,
                     const std::vector<DisplayMode> &display_modes,
                     const std::string &clientID) {
  int pair_status = pair_handler.isPaired(clientID);
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

std::pair<pt::ptree, std::string>
pair_get_server_cert(const std::string &user_pin, const std::string &salt, const X509 &server_cert) {
  pt::ptree resp;

  auto key = crypto::sha256(salt + user_pin);
  auto cert_pem = crypto::pem(server_cert);
  auto cert_hex = crypto::str_to_hex(cert_pem);

  resp.put("root.paired", 1);
  resp.put("root.plaincert", cert_hex);
  resp.put("root.<xmlattr>.status_code", 200);

  return std::make_pair(resp, key);
}

std::pair<pt::ptree, std::string>
pair_send_server_challenge(const std::string &aes_key, const std::string &client_challenge, const X509 &server_cert) {
  pt::ptree resp;
  std::string server_secret;

  // TODO: fill server_secret
  // auto encrypted_response = util::from_hex_vec(args.at("serverchallengeresp"s), true);

  // std::vector<uint8_t> decrypted;
  // crypto::cipher::ecb_t cipher(*sess.cipher_key, false);

  // cipher.aes_decrypt_cbc(encrypted_response, decrypted);

  // sess.clienthash = std::move(decrypted);

  // auto serversecret = sess.serversecret;
  // auto sign = crypto::sign256(crypto::pkey(conf_intern.pkey), serversecret);

  // serversecret.insert(std::end(serversecret), std::begin(sign), std::end(sign));

  resp.put("root.pairingsecret", crypto::str_to_hex(server_secret));
  resp.put("root.paired", 1);
  resp.put("root.<xmlattr>.status_code", 200);

  return std::make_pair(resp, server_secret);
}

} // namespace moonlight