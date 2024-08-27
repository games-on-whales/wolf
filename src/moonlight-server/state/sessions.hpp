#pragma once

#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <immer/vector.hpp>
#include <optional>
#include <range/v3/view.hpp>

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

inline immer::vector<events::StreamSession> remove_session(const immer::vector<events::StreamSession> &sessions,
                                                           const events::StreamSession &session) {
  return sessions                                                                                           //
         | ranges::views::filter([remove_hash = session.session_id](const events::StreamSession &cur_ses) { //
             return cur_ses.session_id != remove_hash;                                                      //
           })                                                                                               //
         | ranges::to<immer::vector<events::StreamSession>>();                                              //
}