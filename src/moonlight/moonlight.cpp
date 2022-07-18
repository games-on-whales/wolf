#include <crypto/crypto.hpp>
#include <moonlight/protocol.hpp>

namespace pt = boost::property_tree;

namespace moonlight {

XML serverinfo(bool isServerBusy,
               int current_appid,
               int https_port,
               int http_port,
               const std::string &uuid,
               const std::string &hostname,
               const std::string &mac_address,
               const std::string &external_ip,
               const std::string &local_ip,
               const immer::array<DisplayMode> &display_modes,
               int pair_status) {
  XML resp;

  resp.put("root.<xmlattr>.status_code", 200);
  resp.put("root.hostname", hostname);

  resp.put("root.appversion", M_VERSION);
  resp.put("root.GfeVersion", M_GFE_VERSION);
  resp.put("root.uniqueid", uuid);

  resp.put("root.MaxLumaPixelsHEVC", "0");
  // TODO: resp.put("root.MaxLumaPixelsHEVC",config::video.hevc_mode > 1 ? "1869449984" : "0");
  resp.put("root.ServerCodecModeSupport", "3"); // TODO: what are the modes here?

  resp.put("root.HttpsPort", https_port);
  resp.put("root.ExternalPort", http_port);
  resp.put("root.mac", mac_address);
  resp.put("root.ExternalIP", external_ip);
  resp.put("root.LocalIP", local_ip);

  XML display_nodes;
  for (auto mode : display_modes) {
    XML display_node;
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
std::pair<XML, std::string>
get_server_cert(const std::string &user_pin, const std::string &salt, const std::string &server_cert_pem) {
  XML resp;

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

std::pair<XML, std::pair<std::string, std::string>> send_server_challenge(const std::string &aes_key,
                                                                          const std::string &client_challenge,
                                                                          const std::string &server_cert_signature,
                                                                          const std::string &server_secret,
                                                                          const std::string &server_challenge) {
  XML resp;

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

std::pair<XML, std::string> get_client_hash(const std::string &aes_key,
                                            const std::string &server_secret,
                                            const std::string &server_challenge_resp,
                                            const std::string &server_cert_private_key) {
  XML resp;

  auto server_challenge_hex = crypto::hex_to_str(server_challenge_resp, true);
  auto decrypted_challenge = crypto::aes_decrypt_ecb(server_challenge_hex, aes_key);
  auto signature = crypto::sign(server_secret, server_cert_private_key);

  resp.put("root.pairingsecret", crypto::str_to_hex(server_secret + signature));
  resp.put("root.paired", 1);
  resp.put("root.<xmlattr>.status_code", 200);

  return std::make_pair(resp, decrypted_challenge);
}

XML client_pair(const std::string &aes_key,
                const std::string &server_challenge,
                const std::string &client_hash,
                const std::string &client_pairing_secret,
                const std::string &client_public_cert_signature,
                const std::string &client_cert_public_key) {
  XML resp;
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

XML applist(const immer::vector<App> &apps) {
  XML resp;
  resp.put("root.<xmlattr>.status_code", 200);

  for (auto &app : apps) {
    XML app_t;

    app_t.put("IsHdrSupported", app.support_hdr ? 1 : 0);
    app_t.put("AppTitle", app.title);
    app_t.put("ID", app.id);

    resp.push_back(std::make_pair("App", std::move(app_t)));
  }

  return resp;
}

XML launch_success(const std::string &local_ip, const std::string &rtsp_port) {
  // TODO: implement resume
  // TODO: implement error on launch
  // TODO: return GCM key
  XML resp;

  resp.put("root.<xmlattr>.status_code", 200);
  resp.put("root.sessionUrl0", "rtsp://" + local_ip + ":" + rtsp_port);
  resp.put("root.gamesession", 1);

  return resp;
}

// TODO: implement key derivation
// stream::launch_session_t make_launch_session(bool host_audio, const args_t &args) {
//  stream::launch_session_t launch_session;
//
//  launch_session.host_audio = host_audio;
//  launch_session.gcm_key    = util::from_hex<crypto::aes_t>(args.at("rikey"s), true);
//  uint32_t prepend_iv       = util::endian::big<uint32_t>(util::from_view(args.at("rikeyid"s)));
//  auto prepend_iv_p         = (uint8_t *)&prepend_iv;
//
//  auto next = std::copy(prepend_iv_p, prepend_iv_p + sizeof(prepend_iv), std::begin(launch_session.iv));
//  std::fill(next, std::end(launch_session.iv), 0);
//
//  return launch_session;
//}

} // namespace moonlight