#pragma once

#include <crypto/crypto.hpp>
#include <functional>
#include <helpers/utils.hpp>
#include <moonlight/protocol.hpp>
#include <range/v3/view.hpp>
#include <rest/helpers.hpp>
#include <rest/rest.hpp>
#include <state/config.hpp>

namespace endpoints {

template <class T> void server_error(std::shared_ptr<typename SimpleWeb::Server<T>::Response> response) {
  XML xml;
  xml.put("root.<xmlattr>.status_code", 400);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_bad_request, xml);
}

template <class T>
void not_found(std::shared_ptr<typename SimpleWeb::Server<T>::Response> response,
               std::shared_ptr<typename SimpleWeb::Server<T>::Request> request) {
  log_req<T>(request);

  XML xml;
  xml.put("root.<xmlattr>.status_code", 404);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_not_found, xml);
}

template <class T>
void serverinfo(std::shared_ptr<typename SimpleWeb::Server<T>::Response> response,
                std::shared_ptr<typename SimpleWeb::Server<T>::Request> request,
                const std::shared_ptr<state::AppState> &state) {
  log_req<T>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();

  auto cfg = state->config;
  auto host = state->host;
  bool is_https = std::is_same_v<SimpleWeb::HTTPS, T>;
  auto xml = moonlight::serverinfo(false, // TODO: isServerBusy
                                   -1,    // TODO: current_appid
                                   state::HTTPS_PORT,
                                   state::HTTP_PORT,
                                   cfg.uuid,
                                   cfg.hostname,
                                   host.mac_address,
                                   host.external_ip,
                                   host.internal_ip,
                                   host.display_modes,
                                   is_https,
                                   cfg.support_hevc);

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

template <class T>
void pair(std::shared_ptr<typename SimpleWeb::Server<T>::Response> response,
          std::shared_ptr<typename SimpleWeb::Server<T>::Request> request,
          const std::shared_ptr<state::AppState> &state) {
  log_req<T>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto salt = get_header(headers, "salt");
  auto client_cert_str = get_header(headers, "clientcert");
  auto client_id = get_header(headers, "uniqueid");
  auto client_ip = request->remote_endpoint().address().to_string();

  if (!client_id) {
    logs::log(logs::warning, "Received pair request without uniqueid, stopping.");
    return;
  }

  /* client_id is hardcoded in Moonlight, we add the IP so that different users can pair at the same time */
  auto cache_key = client_id.value() + "@" + client_ip;

  // PHASE 1
  if (client_id && salt && client_cert_str) {
    auto future_pin = std::promise<std::string>();
    state::PairSignal signal = {client_ip, future_pin};
    state->event_bus->fire_event(signal); // Emit a signal and wait for a response
    auto user_pin = future_pin.get_future().get();

    auto server_pem = x509::get_cert_pem(*state->host.server_cert);
    auto result = moonlight::pair::get_server_cert(user_pin, salt.value(), server_pem);

    auto client_cert_parsed = crypto::hex_to_str(client_cert_str.value(), true);

    state->pairing_cache.update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.set(cache_key,
                               {client_id.value(),
                                client_cert_parsed,
                                state::RTSP_SETUP_PORT,
                                state::CONTROL_PORT,
                                state::VIDEO_STREAM_PORT,
                                state::AUDIO_STREAM_PORT,
                                result.second,
                                std::nullopt,
                                std::nullopt});
    });

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, result.first);
    return;
  }

  auto client_cache_it = state->pairing_cache.load()->find(cache_key);
  if (client_cache_it == nullptr) {
    logs::log(logs::warning, "Unable to find {} {} in the pairing cache", client_id.value(), client_ip);
    return;
  }
  auto client_cache = *client_cache_it;

  // PHASE 2
  auto client_challenge = get_header(headers, "clientchallenge");
  if (client_challenge) {

    auto server_cert_signature = x509::get_cert_signature(state->host.server_cert);
    auto [xml, server_secret_pair] =
        moonlight::pair::send_server_challenge(client_cache.aes_key, client_challenge.value(), server_cert_signature);

    auto [server_secret, server_challenge] = server_secret_pair;
    client_cache.server_secret = server_secret;
    client_cache.server_challenge = server_challenge;
    state->pairing_cache.update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.set(cache_key, client_cache);
    });

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  // PHASE 3
  auto server_challenge = get_header(headers, "serverchallengeresp");
  if (server_challenge && client_cache.server_secret) {

    auto [xml, client_hash] = moonlight::pair::get_client_hash(
        client_cache.aes_key,
        client_cache.server_secret.value(),
        server_challenge.value(),
        x509::get_pkey_content(const_cast<EVP_PKEY *>(state->host.server_pkey)));

    client_cache.client_hash = client_hash;

    state->pairing_cache.update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.set(cache_key, client_cache);
    });

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  // PHASE 4
  auto client_secret = get_header(headers, "clientpairingsecret");
  if (client_secret && client_cache.server_challenge && client_cache.client_hash) {
    auto client_cert = x509::cert_from_string({client_cache.client_cert});

    auto xml = moonlight::pair::client_pair(client_cache.aes_key,
                                            client_cache.server_challenge.value(),
                                            client_cache.client_hash.value(),
                                            client_secret.value(),
                                            x509::get_cert_signature(client_cert),
                                            x509::get_cert_public_key(client_cert));

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);

    auto is_paired = xml.template get<int>("root.paired");
    if (is_paired == 1) {
      state::pair(state->config, {client_id.value(), client_cache.client_cert});
      logs::log(logs::info, "Succesfully paired {}", client_ip);
    } else {
      logs::log(logs::warning, "Failed pairing with {}", client_ip);
    }
    return;
  }

  // PHASE 5 (over HTTPS)
  auto phrase = get_header(headers, "phrase");
  if (phrase && phrase.value() == "pairchallenge") {
    XML xml;

    xml.put("root.paired", 1);
    xml.put("root.<xmlattr>.status_code", 200);

    // Cleanup temporary pairing_cache
    state->pairing_cache.update([&cache_key](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.erase(cache_key);
    });

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  logs::log(logs::warning, "Unable to match pair with any phase, you can retry pairing from Moonlight");
}

namespace https {

template <class T>
void applist(std::shared_ptr<typename SimpleWeb::Server<T>::Response> response,
             std::shared_ptr<typename SimpleWeb::Server<T>::Request> request,
             const std::shared_ptr<state::AppState> &state) {
  log_req<T>(request);

  auto base_apps = state->config.apps                                            //
                   | ranges::views::transform([](auto app) { return app.base; }) //
                   | ranges::to<immer::vector<moonlight::App>>();
  auto xml = moonlight::applist(base_apps);

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

template <class T>
void launch(std::shared_ptr<typename SimpleWeb::Server<T>::Response> response,
            std::shared_ptr<typename SimpleWeb::Server<T>::Request> request,
            const state::PairedClient &current_client,
            const std::shared_ptr<state::AppState> &state) {
  log_req<T>(request);

  // TODO: check if this stuff is valid?
  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto display_mode_str = utils::split(get_header(headers, "mode").value(), 'x');
  moonlight::DisplayMode display_mode = {std::stoi(display_mode_str[0].data()),
                                         std::stoi(display_mode_str[1].data()),
                                         std::stoi(display_mode_str[2].data())};

  // stereo TODO: what should we select here?
  state::AudioMode audio_mode = {2, 1, 1, {state::AudioMode::FRONT_LEFT, state::AudioMode::FRONT_RIGHT}};

  auto client_ip = request->remote_endpoint().address().to_string();
  auto session_id = std::hash<std::string>{}(current_client.client_cert);

  state::StreamSession session = {.session_id = session_id,
                                  .event_bus = state->event_bus,
                                  .display_mode = display_mode,
                                  .audio_mode = audio_mode,
                                  .app = state::get_app_by_id(state->config, get_header(headers, "appid").value()),
                                  // gcm encryption keys
                                  .gcm_key = get_header(headers, "rikey").value(),
                                  .gcm_iv_key = get_header(headers, "rikeyid").value(),
                                  // client info
                                  .unique_id = get_header(headers, "uuid").value(),
                                  .ip = client_ip,
                                  // ports
                                  .rtsp_port = current_client.rtsp_port,
                                  .control_port = current_client.control_port,
                                  .audio_port = current_client.audio_port,
                                  .video_port = current_client.video_port};
  immer::box<state::StreamSession> shared_session = {session};

  state->event_bus->fire_event(shared_session); // Anyone listening for this event will be called

  auto xml = moonlight::launch_success(state->host.external_ip, std::to_string(current_client.rtsp_port));
  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}
} // namespace https

} // namespace endpoints