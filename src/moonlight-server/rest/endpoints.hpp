#pragma once

#include <control/control.hpp>
#include <crypto/crypto.hpp>
#include <events/events.hpp>
#include <filesystem>
#include <functional>
#include <helpers/utils.hpp>
#include <immer/vector_transient.hpp>
#include <moonlight/control.hpp>
#include <moonlight/protocol.hpp>
#include <platforms/hw.hpp>
#include <range/v3/view.hpp>
#include <rest/helpers.hpp>
#include <rest/rest.hpp>
#include <rtp/udp-ping.hpp>
#include <state/config.hpp>
#include <state/sessions.hpp>
#include <utility>

namespace endpoints {

using namespace control;
using namespace wolf::core;

static std::size_t get_client_id(const state::PairedClient &current_client) {
  return std::hash<std::string>{}(current_client.client_cert);
}

template <class T> void server_error(const std::shared_ptr<typename SimpleWeb::Server<T>::Response> &response) {
  XML xml;
  xml.put("root.<xmlattr>.status_code", 400);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_bad_request, xml);
}

template <class T>
void not_found(const std::shared_ptr<typename SimpleWeb::Server<T>::Response> &response,
               const std::shared_ptr<typename SimpleWeb::Server<T>::Request> &request) {
  log_req<T>(request);

  XML xml;
  xml.put("root.<xmlattr>.status_code", 404);
  send_xml<T>(response, SimpleWeb::StatusCode::client_error_not_found, xml);
}

template <class T>
std::string get_host_ip(const std::shared_ptr<typename SimpleWeb::Server<T>::Request> &request,
                        const immer::box<state::AppState> &state) {
  return state->host->internal_ip.value_or(request->local_endpoint().address().to_string());
}

template <class T>
void serverinfo(const std::shared_ptr<typename SimpleWeb::Server<T>::Response> &response,
                const std::shared_ptr<typename SimpleWeb::Server<T>::Request> &request,
                const immer::box<state::AppState> &state) {
  log_req<T>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();

  auto cfg = state->config;
  auto host = state->host;
  bool is_https = std::is_same_v<SimpleWeb::HTTPS, T>;

  auto session = get_session_by_ip(state->running_sessions->load(), get_client_ip<T>(request));
  bool is_busy = session.has_value();
  int app_id = session.has_value() ? std::stoi(session->app->base.id) : 0;

  auto local_ip = get_host_ip<T>(request, state);

  auto xml = moonlight::serverinfo(is_busy,
                                   app_id,
                                   state::HTTPS_PORT,
                                   state::HTTP_PORT,
                                   cfg->uuid,
                                   cfg->hostname,
                                   utils::lazy_value_or(host->mac_address, [&]() { return get_mac_address(local_ip); }),
                                   local_ip,
                                   host->display_modes,
                                   is_https,
                                   cfg->support_hevc,
                                   cfg->support_av1);

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

template <class T>
void pair(const std::shared_ptr<typename SimpleWeb::Server<T>::Response> &response,
          const std::shared_ptr<typename SimpleWeb::Server<T>::Request> &request,
          const immer::box<state::AppState> &state) {
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
    auto future_pin = std::make_shared<boost::promise<std::string>>();
    state->event_bus->fire_event( // Emit a signal and wait for the promise to be fulfilled
        immer::box<events::PairSignal>(events::PairSignal{.client_ip = client_ip,
                                                          .host_ip = get_host_ip<T>(request, state),
                                                          .user_pin = future_pin}));

    future_pin->get_future().then(
        [state, salt, client_cert_str, cache_key, client_id, response](boost::future<std::string> fut_pin) {
          auto server_pem = x509::get_cert_pem(state->host->server_cert);
          auto result = moonlight::pair::get_server_cert(fut_pin.get(), salt.value(), server_pem);

          auto client_cert_parsed = crypto::hex_to_str(client_cert_str.value(), true);

          state->pairing_cache->update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
            return pairing_cache.set(cache_key, {.client_cert = client_cert_parsed, .aes_key = result.second});
          });

          send_xml<T>(response, SimpleWeb::StatusCode::success_ok, result.first);
        });

    return;
  }

  auto client_cache_it = state->pairing_cache->load()->find(cache_key);
  if (client_cache_it == nullptr) {
    logs::log(logs::warning, "Unable to find {} {} in the pairing cache", client_id.value(), client_ip);
    return;
  }
  auto client_cache = *client_cache_it;

  // PHASE 2
  auto client_challenge = get_header(headers, "clientchallenge");
  if (client_challenge) {

    auto server_cert_signature = x509::get_cert_signature(state->host->server_cert);
    auto [xml, server_secret_pair] =
        moonlight::pair::send_server_challenge(client_cache.aes_key, client_challenge.value(), server_cert_signature);

    auto [server_secret, server_challenge] = server_secret_pair;
    client_cache.server_secret = server_secret;
    client_cache.server_challenge = server_challenge;
    state->pairing_cache->update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.set(cache_key, client_cache);
    });

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  // PHASE 3
  auto server_challenge = get_header(headers, "serverchallengeresp");
  if (server_challenge && client_cache.server_secret) {

    auto [xml, client_hash] = moonlight::pair::get_client_hash(client_cache.aes_key,
                                                               client_cache.server_secret.value(),
                                                               server_challenge.value(),
                                                               x509::get_pkey_content(state->host->server_pkey));

    client_cache.client_hash = client_hash;

    state->pairing_cache->update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
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
      state::pair(
          state->config,
          state::PairedClient{.client_cert = client_cache.client_cert,
                              .app_state_folder = std::to_string(std::hash<std::string>{}(client_cache.client_cert))});
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
    state->pairing_cache->update([&cache_key](const immer::map<std::string, state::PairCache> &pairing_cache) {
      return pairing_cache.erase(cache_key);
    });

    send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
    return;
  }

  logs::log(logs::warning, "Unable to match pair with any phase, you can retry pairing from Moonlight");
}

namespace https {

void applist(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
             const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
             const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  auto base_apps = state->config->apps                                           //
                   | ranges::views::transform([](auto app) { return app.base; }) //
                   | ranges::to<immer::vector<moonlight::App>>();
  auto xml = moonlight::applist(base_apps);

  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

events::StreamSession create_run_session(const SimpleWeb::CaseInsensitiveMultimap &headers,
                                         const std::string &client_ip,
                                         const state::PairedClient &current_client,
                                         immer::box<state::AppState> state,
                                         const events::App &run_app) {
  auto display_mode_str = utils::split(get_header(headers, "mode").value_or("1920x1080x60"), 'x');
  moonlight::DisplayMode display_mode = {std::stoi(display_mode_str[0].data()),
                                         std::stoi(display_mode_str[1].data()),
                                         std::stoi(display_mode_str[2].data()),
                                         state->config->support_hevc,
                                         state->config->support_av1};

  auto surround_info = std::stoi(get_header(headers, "surroundAudioInfo").value_or("196610"));
  int channelCount = surround_info & (0xffff /* last 16 bits */);

  std::string host_state_folder = utils::get_env("HOST_APPS_STATE_FOLDER", "/etc/wolf");
  auto full_path = std::filesystem::path(host_state_folder) / current_client.app_state_folder / run_app.base.title;
  logs::log(logs::debug, "Host app state folder: {}, creating paths", full_path.string());
  std::filesystem::create_directories(full_path);

  auto video_stream_port = get_next_available_port(state->running_sessions->load(), true);
  auto audio_stream_port = get_next_available_port(state->running_sessions->load(), false);

  return events::StreamSession{.display_mode = display_mode,
                               .audio_channel_count = channelCount,
                               .event_bus = state->event_bus,
                               .app = std::make_shared<events::App>(run_app),
                               .app_state_folder = full_path.string(),

                               // gcm encryption keys
                               .aes_key = get_header(headers, "rikey").value(),
                               .aes_iv = get_header(headers, "rikeyid").value(),

                               // client info
                               .session_id = get_client_id(current_client),
                               .ip = client_ip,
                               .video_stream_port = video_stream_port,
                               .audio_stream_port = audio_stream_port};
}

void start_rtp_ping(const immer::box<events::StreamSession> &session) {

  // Video RTP Ping
  rtp::wait_for_ping(session->video_stream_port,
                     [ev_bus = session->event_bus](unsigned short client_port, const std::string &client_ip) {
                       logs::log(logs::trace, "[PING] video from {}:{}", client_ip, client_port);
                       auto ev = events::RTPVideoPingEvent{.client_ip = client_ip, .client_port = client_port};
                       ev_bus->fire_event(immer::box<events::RTPVideoPingEvent>(ev));
                     });

  // Audio RTP Ping
  rtp::wait_for_ping(session->audio_stream_port,
                     [ev_bus = session->event_bus](unsigned short client_port, const std::string &client_ip) {
                       logs::log(logs::trace, "[PING] audio from {}:{}", client_ip, client_port);
                       auto ev = events::RTPAudioPingEvent{.client_ip = client_ip, .client_port = client_port};
                       ev_bus->fire_event(immer::box<events::RTPAudioPingEvent>(ev));
                     });
}

void launch(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto app = state::get_app_by_id(state->config, get_header(headers, "appid").value());
  auto client_ip = get_client_ip<SimpleWeb::HTTPS>(request);
  auto new_session = create_run_session(request->parse_query_string(), client_ip, current_client, state, app);
  state->event_bus->fire_event(immer::box<events::StreamSession>(new_session));
  state->running_sessions->update(
      [&new_session](const immer::vector<events::StreamSession> &ses_v) { return ses_v.push_back(new_session); });

  start_rtp_ping(new_session);

  auto xml =
      moonlight::launch_success(get_host_ip<SimpleWeb::HTTPS>(request, state), std::to_string(state::RTSP_SETUP_PORT));
  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

void resume(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  auto client_ip = get_client_ip<SimpleWeb::HTTPS>(request);
  auto old_session = get_session_by_ip(state->running_sessions->load(), client_ip);
  if (old_session) {
    auto new_session =
        create_run_session(request->parse_query_string(), client_ip, current_client, state, *old_session->app);
    // Carry over the old session display handle
    new_session.wayland_display = std::move(old_session->wayland_display);
    // Carry over the old session devices, they'll be already plugged into the container
    new_session.mouse = std::move(old_session->mouse);
    new_session.keyboard = std::move(old_session->keyboard);
    new_session.joypads = std::move(old_session->joypads);
    new_session.pen_tablet = std::move(old_session->pen_tablet);
    new_session.touch_screen = std::move(old_session->touch_screen);

    start_rtp_ping(new_session);

    state->running_sessions->update([&old_session, &new_session](const immer::vector<events::StreamSession> ses_v) {
      return remove_session(ses_v, old_session.value()).push_back(new_session);
    });
  } else {
    logs::log(logs::warning, "[HTTPS] Received resume event from an unregistered session, ip: {}", client_ip);
  }

  XML xml;
  xml.put("root.<xmlattr>.status_code", 200);
  xml.put("root.sessionUrl0",
          "rtsp://"s + get_host_ip<SimpleWeb::HTTPS>(request, state) + ':' + std::to_string(state::RTSP_SETUP_PORT));
  xml.put("root.resume", 1);
  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

void cancel(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  auto client_ip = get_client_ip<SimpleWeb::HTTPS>(request);
  auto client_session = get_session_by_ip(state->running_sessions->load(), client_ip);
  if (client_session) {
    state->event_bus->fire_event(
        immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = client_session->session_id}));

    state->running_sessions->update([&client_session](const immer::vector<events::StreamSession> &ses_v) {
      return remove_session(ses_v, client_session.value());
    });
  } else {
    logs::log(logs::warning, "[HTTPS] Received resume event from an unregistered session, ip: {}", client_ip);
  }

  XML xml;
  xml.put("root.<xmlattr>.status_code", 200);
  xml.put("root.cancel", 1);
  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

} // namespace https

} // namespace endpoints