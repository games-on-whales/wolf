#pragma once
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <rtsp/utils.hpp>

namespace rtsp::commands {

using namespace ranges;

msg_t options(msg_t req) {
  RTSP_MESSAGE_OPTION options = {"CSeq", std::to_string(req->sequenceNumber)};
  return create_rtsp_msg({options}, 200, "OK", req->sequenceNumber, {});
}

msg_t describe(msg_t req, const state::StreamSession &session) {
  RTSP_MESSAGE_OPTION options = {"CSeq", std::to_string(req->sequenceNumber)};

  auto video_params = "";
  if (session.display_mode.hevc_supported) {
    video_params = "sprop-parameter-sets=AAAAAU";
  }

  std::string audio_speakers = session.audio_mode.speakers                                           //
                               | view::transform([](auto speaker) { return (char)(speaker + '0'); }) //
                               | to<std::string>;                                                    //

  auto audio_params = fmt::format("a=fmtp:97 surround-params={}{}{}{}",
                                  session.audio_mode.channels,
                                  session.audio_mode.streams,
                                  session.audio_mode.coupled_streams,
                                  audio_speakers);

  auto payload = fmt::format("{}\n{}\n", video_params, audio_params);
  return create_rtsp_msg({options}, 200, "OK", req->sequenceNumber, payload);
}

msg_t setup(msg_t req, const state::StreamSession &session) {

  int service_port;
  auto type = utils::sub_string({req->message.request.target}, '=', '/');
  logs::log(logs::trace, "[RTSP] setup type: {}", type);

  switch (utils::hash(type)) {
  case utils::hash("audio"):
    service_port = session.audio_port;
    break;
  case utils::hash("video"):
    service_port = session.video_port;
    break;
  case utils::hash("control"):
    service_port = session.control_port;
    break;
  default:
    return create_error_msg(404, "NOT FOUND", req->sequenceNumber);
  }

  auto session_opt = "DEADBEEFCAFE;timeout = 90"s;
  std::vector<RTSP_MESSAGE_OPTION> options = {{"CSeq", std::to_string(req->sequenceNumber)},
                                              {"Session", session_opt},
                                              {"Transport", std::to_string(service_port)}};

  return create_rtsp_msg(options, 200, "OK", req->sequenceNumber, {});
}

msg_t message_handler(msg_t req, const state::StreamSession &session) {
  auto cmd = std::string_view(req.get()->message.request.command);
  logs::log(logs::debug, "[RTSP] received command {}", cmd);

  switch (utils::hash(cmd)) {
  case utils::hash("OPTIONS"):
    return options(std::move(req));
  case utils::hash("DESCRIBE"):
    return describe(std::move(req), session);
  case utils::hash("SETUP"):
    return setup(std::move(req), session);
    //  case utils::hash("ANNOUNCE"):
    //    break;
    //  case utils::hash("PLAY"):
    //    break;
  default:
    logs::log(logs::warning, "[RTSP] command {} not found", cmd);
    return create_error_msg(404, "NOT FOUND", req->sequenceNumber);
  }
}

} // namespace rtsp::commands