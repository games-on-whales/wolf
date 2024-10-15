#pragma once

#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <immer/vector.hpp>
#include <optional>
#include <range/v3/view.hpp>
#include <state/config.hpp>
#include <state/serialised_config.hpp>

namespace state {

using namespace wolf::core;

inline std::optional<events::StreamSession> get_session_by_ip(const immer::vector<events::StreamSession> &sessions,
                                                              const std::string &ip) {
  auto results = sessions |                                                                                      //
                 ranges::views::filter([&ip](const events::StreamSession &session) { return session.ip == ip; }) //
                 | ranges::views::take(1)                                                                        //
                 | ranges::to_vector;                                                                            //
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple sessions for a given IP: {}", ip);
    return {};
  }
}

inline std::optional<events::StreamSession> get_session_by_id(const immer::vector<events::StreamSession> &sessions,
                                                              const std::size_t id) {
  auto results =
      sessions |                                                                                             //
      ranges::views::filter([id](const events::StreamSession &session) { return session.session_id == id; }) //
      | ranges::views::take(1)                                                                               //
      | ranges::to_vector;                                                                                   //
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple sessions for a given ID: {}", id);
    return {};
  }
}

inline std::optional<events::StreamSession> get_session_by_client(const immer::vector<events::StreamSession> &sessions,
                                                                  const wolf::config::PairedClient &client) {
  auto client_id = get_client_id(client);
  return get_session_by_id(sessions, client_id);
}

inline unsigned short get_next_available_port(const immer::vector<events::StreamSession> &sessions, bool video) {
  auto ports = sessions |                                                               //
               ranges::views::transform([video](const events::StreamSession &session) { //
                 return video ? session.video_stream_port : session.audio_stream_port;  //
               })                                                                       //
               | ranges::to_vector;
  unsigned short port = video ? state::VIDEO_PING_PORT : state::AUDIO_PING_PORT;
  while (std::find(ports.begin(), ports.end(), port) != ports.end()) {
    port++;
  }
  return port;
}

inline std::shared_ptr<events::StreamSession> create_stream_session(immer::box<state::AppState> state,
                                                                    const events::App &run_app,
                                                                    const wolf::config::PairedClient &current_client,
                                                                    const moonlight::DisplayMode &display_mode,
                                                                    int audio_channel_count) {
  std::string host_state_folder = utils::get_env("HOST_APPS_STATE_FOLDER", "/etc/wolf");
  auto full_path = std::filesystem::path(host_state_folder) / current_client.app_state_folder / run_app.base.title;
  logs::log(logs::debug, "Host app state folder: {}, creating paths", full_path.string());
  std::filesystem::create_directories(full_path);

  auto video_stream_port = get_next_available_port(state->running_sessions->load(), true);
  auto audio_stream_port = get_next_available_port(state->running_sessions->load(), false);

  auto session = events::StreamSession{.display_mode = display_mode,
                                       .audio_channel_count = audio_channel_count,
                                       .event_bus = state->event_bus,
                                       .client_settings = current_client.settings,
                                       .app = std::make_shared<events::App>(run_app),
                                       .app_state_folder = full_path.string(),

                                       // client info
                                       .session_id = state::get_client_id(current_client),
                                       .video_stream_port = video_stream_port,
                                       .audio_stream_port = audio_stream_port};

  return std::make_shared<events::StreamSession>(session);
}

inline immer::vector<events::StreamSession> remove_session(const immer::vector<events::StreamSession> &sessions,
                                                           const events::StreamSession &session) {
  return sessions                                                                                           //
         | ranges::views::filter([remove_hash = session.session_id](const events::StreamSession &cur_ses) { //
             return cur_ses.session_id != remove_hash;                                                      //
           })                                                                                               //
         | ranges::to<immer::vector<events::StreamSession>>();                                              //
}
} // namespace state