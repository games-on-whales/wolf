#pragma once

#include <control/control.hpp>
#include <crypto/crypto.hpp>
#include <curl/curl.h>
#include <curl/easy.h>
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
#include <state/utils.hpp>
#include <utility>

namespace endpoints {

using namespace control;
using namespace wolf::core;

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
                std::optional<events::StreamSession> stream_session,
                const immer::box<state::AppState> &state) {
  log_req<T>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();

  auto cfg = state->config;
  auto host = state->host;
  bool is_https = std::is_same_v<SimpleWeb::HTTPS, T>;

  bool is_busy = stream_session.has_value();
  int app_id = stream_session.has_value() ? std::stoi(stream_session->app->base.id) : 0;

  auto local_ip = get_host_ip<T>(request, state);

  auto xml = moonlight::serverinfo(is_busy,
                                   app_id,
                                   get_port(state::HTTPS_PORT),
                                   get_port(state::HTTP_PORT),
                                   cfg->uuid,
                                   cfg->hostname,
                                   utils::lazy_value_or(host->mac_address, [&]() { return get_mac_address(local_ip); }),
                                   local_ip,
                                   host->display_modes,
                                   is_https,
                                   cfg->support_hevc,
                                   cfg->support_av1,
                                   cfg->support_hdr);

  send_xml<T>(response, SimpleWeb::StatusCode::success_ok, xml);
}

void remove_pair_session(const immer::box<state::AppState> &state, const std::string &cache_key) {
  state->pairing_cache->update([&cache_key](const immer::map<std::string, state::PairCache> &pairing_cache) {
    return pairing_cache.erase(cache_key);
  });
}

XML fail_pair(const std::string &status_msg) {
  logs::log(logs::warning, "Failed pairing: {}", status_msg);

  XML tree;
  tree.put("root.paired", 0);
  tree.put("root.<xmlattr>.status_code", 400);
  tree.put("root.<xmlattr>.status_message", status_msg);

  return tree;
}

struct XMLResult {
  SimpleWeb::StatusCode status;
  XML xml;
};

std::shared_ptr<boost::promise<XMLResult>> pair_phase1(const immer::box<state::AppState> &state,
                                                       const std::string &client_ip,
                                                       const std::string &host_ip,
                                                       const std::string &client_cert_str,
                                                       const std::string &salt,
                                                       const std::string &cache_key) {
  auto future_result = std::make_shared<boost::promise<XMLResult>>();
  if (state->pairing_cache->load()->find(cache_key)) {
    future_result->set_value(
        {SimpleWeb::StatusCode::client_error_bad_request, fail_pair("Out of order pair request (phase 1)")});
    remove_pair_session(state, cache_key);
    return future_result;
  }

  auto future_pin = std::make_shared<boost::promise<std::string>>();
  state->event_bus->fire_event( // Emit a signal and wait for the promise to be fulfilled
      immer::box<events::PairSignal>(
          events::PairSignal{.client_ip = client_ip, .host_ip = host_ip, .user_pin = future_pin}));

  future_pin->get_future().then(
      [state, salt, client_cert_str, cache_key, future_result](boost::future<std::string> fut_pin) {
        auto server_pem = x509::get_cert_pem(state->host->server_cert);
        auto result = moonlight::pair::get_server_cert(fut_pin.get(), salt, server_pem);

        auto client_cert_parsed = crypto::hex_to_str(client_cert_str, true);

        state->pairing_cache->update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
          return pairing_cache.set(cache_key,
                                   {.client_cert = client_cert_parsed,
                                    .aes_key = result.second,
                                    .last_phase = state::PAIR_PHASE::GETSERVERCERT});
        });

        future_result->set_value({SimpleWeb::StatusCode::success_ok, result.first});
      });

  return future_result;
}

XMLResult pair_phase2(const immer::box<state::AppState> &state,
                      state::PairCache &client_cache,
                      const std::string &client_challenge,
                      const std::string &cache_key) {
  if (client_cache.last_phase != state::PAIR_PHASE::GETSERVERCERT) {
    return {SimpleWeb::StatusCode::client_error_bad_request, fail_pair("Out of order pair request (phase 2)")};
  }
  client_cache.last_phase = state::PAIR_PHASE::CLIENTCHALLENGE;

  auto server_cert_signature = x509::get_cert_signature(state->host->server_cert);
  auto [xml, server_secret_pair] =
      moonlight::pair::send_server_challenge(client_cache.aes_key, client_challenge, server_cert_signature);

  auto [server_secret, server_challenge] = server_secret_pair;
  client_cache.server_secret = server_secret;
  client_cache.server_challenge = server_challenge;
  state->pairing_cache->update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
    return pairing_cache.set(cache_key, client_cache);
  });

  return {SimpleWeb::StatusCode::success_ok, xml};
}

XMLResult pair_phase3(const immer::box<state::AppState> &state,
                      state::PairCache &client_cache,
                      const std::string &server_challenge,
                      const std::string &cache_key) {
  if (client_cache.last_phase != state::PAIR_PHASE::CLIENTCHALLENGE) {
    return {SimpleWeb::StatusCode::client_error_bad_request, fail_pair("Out of order pair request (phase 3)")};
  }
  client_cache.last_phase = state::PAIR_PHASE::SERVERCHALLENGERESP;

  auto [xml, client_hash] = moonlight::pair::get_client_hash(client_cache.aes_key,
                                                             client_cache.server_secret.value(),
                                                             server_challenge,
                                                             x509::get_pkey_content(state->host->server_pkey));
  client_cache.client_hash = client_hash;
  state->pairing_cache->update([&](const immer::map<std::string, state::PairCache> &pairing_cache) {
    return pairing_cache.set(cache_key, client_cache);
  });
  return {SimpleWeb::StatusCode::success_ok, xml};
}

XMLResult pair_phase4(state::PairCache &client_cache, const std::string &client_secret) {
  if (client_cache.last_phase != state::PAIR_PHASE::SERVERCHALLENGERESP) {
    return {SimpleWeb::StatusCode::client_error_bad_request, fail_pair("Out of order pair request (phase 4)")};
  }
  client_cache.last_phase = state::PAIR_PHASE::CLIENTPAIRINGSECRET;

  auto client_cert = x509::cert_from_string(client_cache.client_cert);

  if (!client_cert) {
    return {SimpleWeb::StatusCode::client_error_bad_request, fail_pair("Unable to parse client certificate")};
  }

  auto client_sig = x509::get_cert_signature(client_cert);
  auto public_key = x509::get_cert_public_key(client_cert);
  auto xml = moonlight::pair::client_pair(client_cache.aes_key,
                                          client_cache.server_challenge.value(),
                                          client_cache.client_hash.value(),
                                          client_secret,
                                          client_sig,
                                          public_key);

  auto is_paired = xml.get<int>("root.paired") == 1;
  return {is_paired ? SimpleWeb::StatusCode::success_ok : SimpleWeb::StatusCode::client_error_bad_request, xml};
}

void pair(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTP>::Response> &response,
          const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTP>::Request> &request,
          const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTP>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto salt = get_header(headers, "salt");
  auto client_cert_str = get_header(headers, "clientcert");
  auto client_id = get_header(headers, "uniqueid");
  auto client_ip = request->remote_endpoint().address().to_string();

  if (!client_id) {
    send_xml<SimpleWeb::HTTP>(response,
                              SimpleWeb::StatusCode::client_error_bad_request,
                              fail_pair("Received pair request without uniqueid, stopping."));
    return;
  }

  /* client_id is hardcoded in Moonlight, we add the IP so that different users can pair at the same time */
  auto cache_key = client_id.value() + "@" + client_ip;

  // PHASE 1
  if (client_id && salt && client_cert_str) {
    auto future_result = pair_phase1(state,
                                     client_ip,
                                     get_host_ip<SimpleWeb::HTTP>(request, state),
                                     client_cert_str.value(),
                                     salt.value(),
                                     cache_key);
    future_result->get_future().then([response](boost::future<XMLResult> result) {
      auto [status, xml] = result.get();
      send_xml<SimpleWeb::HTTP>(response, status, xml);
    });
    return;
  }

  auto client_cache_it = state->pairing_cache->load()->find(cache_key);
  if (client_cache_it == nullptr) {
    send_xml<SimpleWeb::HTTP>(
        response,
        SimpleWeb::StatusCode::client_error_bad_request,
        fail_pair(fmt::format("Unable to find {} {} in the pairing cache", client_id.value(), client_ip)));
    return;
  }
  auto client_cache = *client_cache_it;

  // PHASE 2
  auto client_challenge = get_header(headers, "clientchallenge");
  if (client_challenge) {
    auto [status, xml] = pair_phase2(state, client_cache, client_challenge.value(), cache_key);
    send_xml<SimpleWeb::HTTP>(response, status, xml);
    if (status != SimpleWeb::StatusCode::success_ok) {
      remove_pair_session(state, cache_key); // security measure, remove the session if the pairing failed
    }
    return;
  }

  // PHASE 3
  auto server_challenge = get_header(headers, "serverchallengeresp");
  if (server_challenge && client_cache.server_secret) {
    auto [status, xml] = pair_phase3(state, client_cache, server_challenge.value(), cache_key);
    send_xml<SimpleWeb::HTTP>(response, status, xml);
    if (status != SimpleWeb::StatusCode::success_ok) {
      remove_pair_session(state, cache_key); // security measure, remove the session if the pairing failed
    }
    return;
  }

  // PHASE 4
  auto client_secret = get_header(headers, "clientpairingsecret");
  if (client_secret && client_cache.server_challenge && client_cache.client_hash) {
    auto [status, xml] = pair_phase4(client_cache, client_secret.value());
    send_xml<SimpleWeb::HTTP>(response, status, xml);

    if (status == SimpleWeb::StatusCode::success_ok) {
      state::pair(
          state->config,
          state::PairedClient{.client_cert = client_cache.client_cert,
                              .app_state_folder = std::to_string(std::hash<std::string>{}(client_cache.client_cert))});
      logs::log(logs::info, "Succesfully paired {}", client_ip);
    } else {
      logs::log(logs::warning, "Failed pairing with {}", client_ip);
    }

    remove_pair_session(state, cache_key); // Either case, this session is done
    return;
  }

  logs::log(logs::warning, "Unable to match pair with any phase, you can retry pairing from Moonlight");
}

namespace https {

/**
 * The check here is implicit, by running over HTTPS we are checking the client certificate
 */
void pair(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
          const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request) {
  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto phrase = get_header(headers, "phrase");
  // PHASE 5 (over HTTPS)
  if (phrase && phrase.value() == "pairchallenge") {
    XML xml;

    xml.put("root.paired", 1);
    xml.put("root.<xmlattr>.status_code", 200);

    send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
  }
}

void applist(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
             const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
             const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  immer::vector<immer::box<events::App>> moonlight_apps =
      state::get_moonlight_profile(state->config).value()->apps->load();
  auto base_apps = moonlight_apps                                                        //
                   | ranges::views::transform([](const auto &app) { return app->base; }) //
                   | ranges::to<immer::vector<moonlight::App>>();
  auto xml = moonlight::applist(base_apps);

  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

void appasset(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
              const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
              const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto app_id = get_header(headers, "appid");
  if (!app_id) {
    logs::log(logs::warning, "[HTTP] Wrong request, missing app_id");
    server_error<SimpleWeb::HTTPS>(response);
    return;
  }

  auto app = state::get_moonlight_app_by_id(state->config, app_id.value());
  if (!app || !app.value()->base.icon_png_path) {
    logs::log(logs::trace, "[HTTP] Can't find icon_png_path for app with id: {}", app_id.value());
    server_error<SimpleWeb::HTTPS>(response);
    return;
  }

  auto icon_path = app.value()->base.icon_png_path.value();
  if (auto icon = utils::get_icon(state->host->local_base_state_folder, icon_path)) {
    SimpleWeb::CaseInsensitiveMultimap asset_headers;
    asset_headers.emplace("Content-Type", "image/png");
    response->write(SimpleWeb::StatusCode::success_ok, icon.value(), asset_headers);
    response->close_connection_after_response = true;
  } else {
    response->write(SimpleWeb::StatusCode::client_error_not_found, "asset not found");
  }
}

auto create_run_session(const SimpleWeb::CaseInsensitiveMultimap &headers,
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

  // The Vulkan zero-copy producer renders the virtual compositor at this resolution,
  // and the Vulkan encoder requires 64px CTB alignment (see rtsp/commands.hpp, which
  // rounds the *encoder* dimension up). The compositor is created HERE, at /launch,
  // before RTSP, so it must be rounded too -- otherwise the producer fills only the
  // requested rows while the encoder reads full 64px CTBs, leaving the extra rows
  // uninitialised (a green bar along the bottom edge). Scoped to the Vulkan producer;
  // the client scales the slightly-larger frame back into its viewport.
  if (run_app.video_producer_buffer_caps.find("VulkanImage") != std::string::npos) {
    auto round_up_64 = [](int v) { return (v + 63) & ~63; };
    display_mode.width = round_up_64(display_mode.width);
    display_mode.height = round_up_64(display_mode.height);
  }

  auto surround_info = std::stoi(get_header(headers, "surroundAudioInfo").value_or("196610"));
  int channelCount = surround_info & (0xffff /* last 16 bits */);

  auto base_session = create_stream_session(state,
                                            run_app,
                                            current_client,
                                            display_mode,
                                            channelCount,
                                            get_header(headers, "rikey").value(),
                                            get_header(headers, "rikeyid").value());

  base_session->ip = client_ip;
  return std::move(base_session);
}

std::string get_rtsp_ip_string(const std::string &local_ip, const events::StreamSession &session) {
  auto use_fake_ip = utils::get_env("WOLF_USE_RTSP_FAKE_IP", "TRUE") == "TRUE"s;
  return use_fake_ip ? session.rtsp_fake_ip : local_ip;
}

// Forward declaration so launch() can delegate to resume() when a session is already running.
void resume(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state);

void launch(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  SimpleWeb::CaseInsensitiveMultimap headers = request->parse_query_string();
  auto app = state::get_moonlight_app_by_id(state->config, get_header(headers, "appid").value());
  if (!app) {
    logs::log(logs::warning, "[HTTP] Requested wrong app_id: not found");
    server_error<SimpleWeb::HTTPS>(response);
    return;
  }

  // Re-launching while a session is already running would start a second runner that removes the
  // live container out from under the first one. Resume the existing session instead.
  if (state::get_session_by_client(state->running_sessions->load(), current_client)) {
    logs::log(logs::info, "[HTTP] Client already has a running session, resuming instead of relaunching");
    resume(response, request, current_client, state);
    return;
  }

  auto client_ip = get_client_ip<SimpleWeb::HTTPS>(request);
  auto new_session = create_run_session(request->parse_query_string(), client_ip, current_client, state, app.value());
  state->event_bus->fire_event(immer::box<events::StreamSession>(*new_session));
  state->running_sessions->update(
      [new_session](const immer::vector<events::StreamSession> &ses_v) { return ses_v.push_back(*new_session); });

  auto rtsp_ip = get_rtsp_ip_string(get_host_ip<SimpleWeb::HTTPS>(request, state), *new_session);
  auto xml = moonlight::launch_success(rtsp_ip, std::to_string(get_port(state::RTSP_SETUP_PORT)));
  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

void resume(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  auto client_ip = get_client_ip<SimpleWeb::HTTPS>(request);
  auto old_session = state::get_session_by_client(state->running_sessions->load(), current_client);
  if (old_session) {
    auto new_session =
        create_run_session(request->parse_query_string(), client_ip, current_client, state, *old_session->app);
    // Carry over the old session display handle
    new_session->wayland_display = std::move(old_session->wayland_display);
    // Carry over the old session devices, they'll be already plugged into the container
    new_session->mouse = std::move(old_session->mouse);
    new_session->keyboard = std::move(old_session->keyboard);
    new_session->joypads = std::move(old_session->joypads);
    new_session->pen_tablet = std::move(old_session->pen_tablet);
    new_session->touch_screen = std::move(old_session->touch_screen);

    state->running_sessions->update([&old_session, new_session](const immer::vector<events::StreamSession> ses_v) {
      return state::remove_session(ses_v, old_session.value()).push_back(*new_session);
    });

    auto rtsp_ip = get_rtsp_ip_string(get_host_ip<SimpleWeb::HTTPS>(request, state), *new_session);
    auto xml = moonlight::launch_resume(rtsp_ip, std::to_string(get_port(state::RTSP_SETUP_PORT)));
    send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
  } else {
    logs::log(logs::warning, "[HTTPS] Received resume event from an unregistered session, ip: {}", client_ip);
  }

  server_error<SimpleWeb::HTTPS>(response);
}

void cancel(const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Response> &response,
            const std::shared_ptr<typename SimpleWeb::Server<SimpleWeb::HTTPS>::Request> &request,
            const state::PairedClient &current_client,
            const immer::box<state::AppState> &state) {
  log_req<SimpleWeb::HTTPS>(request);

  auto client_session = state::get_session_by_client(state->running_sessions->load(), current_client);
  if (client_session) {
    state->event_bus->fire_event(
        immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = client_session->session_id}));

    state->running_sessions->update([&client_session](const immer::vector<events::StreamSession> &ses_v) {
      return state::remove_session(ses_v, client_session.value());
    });
  } else {
    auto client_ip = get_client_ip<SimpleWeb::HTTPS>(request);
    logs::log(logs::warning, "[HTTPS] Received resume event from an unregistered session, ip: {}", client_ip);
  }

  XML xml;
  xml.put("root.<xmlattr>.status_code", 200);
  xml.put("root.cancel", 1);
  send_xml<SimpleWeb::HTTPS>(response, SimpleWeb::StatusCode::success_ok, xml);
}

} // namespace https

} // namespace endpoints