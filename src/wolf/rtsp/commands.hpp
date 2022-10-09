#pragma once
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <rtsp/utils.hpp>
#include <state/data-structures.hpp>

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

  std::string audio_speakers = session.audio_mode.speakers                                            //
                               | views::transform([](auto speaker) { return (char)(speaker + '0'); }) //
                               | to<std::string>;                                                     //

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
                                              {"Transport", "server_port=" + std::to_string(service_port)}};

  return create_rtsp_msg(options, 200, "OK", req->sequenceNumber, {});
}

/**
 * Ex given: a=x-nv-video[0].clientViewportWd:1920
 * returns: <x-nv-video[0].clientViewportWd, 1920>
 */
std::pair<std::string_view, std::optional<int>> parse_arg_line(std::string_view line) {
  auto split = utils::split(line, ':');
  std::optional<int> val;
  try {
    val = std::stoi(split[1].data());
  } catch (std::exception const &ex) {
    logs::log(logs::warning, "[RTSP] Unable to parse line: {} error: {}", line, ex.what());
    val = {};
  }
  return std::make_pair(split[0].substr(2), // removing "a="
                        val);
}

msg_t announce(msg_t req, const state::StreamSession &session) {
  RTSP_MESSAGE_OPTION options = {"CSeq", std::to_string(req->sequenceNumber)};

  auto splitted_args = utils::split(req->payload, '\n');                // See tests for an example payload
  auto args = splitted_args                                             //
              | views::filter([](auto line) { return line[0] == 'a'; }) // all args start with a=
              | views::transform(parse_arg_line)                        // turns an arg line into a pair
              | to<std::map<std::string_view, std::optional<int>>>;     // to map

  // Control session
  state::ControlSession ctrl = {.session_id = session.session_id,
                                .event_bus = session.event_bus,

                                .port = session.control_port,
                                .peers = 4, // TODO: peers from config?
                                .protocol_type = args["x-nv-general.useReliableUdp"].value_or(0),
                                .aes_key = session.gcm_key,
                                .aes_iv = session.gcm_iv_key};
  session.event_bus->fire_event(immer::box<state::ControlSession>(ctrl));

  // Video session
  moonlight::DisplayMode display = {
      .width = args["x-nv-video[0].clientViewportWd"].value(),
      .height = args["x-nv-video[0].clientViewportHt"].value(),
      .refreshRate = args["x-nv-video[0].maxFPS"].value(),
      .hevc_supported = static_cast<bool>(args["x-nv-clientSupportHevc"].value()),
  };
  auto video_format_h264 = args["x-nv-vqos[0].bitStreamFormat"].value() == 0;
  state::VideoSession video = {
      .display_mode = display,
      .video_format_h264 = video_format_h264,
      .gst_pipeline = video_format_h264 ? session.app.h264_gst_pipeline : session.app.hevc_gst_pipeline,

      .session_id = session.session_id,
      .event_bus = session.event_bus,

      .port = session.video_port,
      .timeout = std::chrono::milliseconds(args["x-nv-video[0].timeoutLengthMs"].value()),
      .packet_size = args["x-nv-video[0].packetSize"].value(),
      .frames_with_invalid_ref_threshold = args["x-nv-video[0].framesWithInvalidRefThreshold"].value(),
      .fec_percentage = 20,
      .min_required_fec_packets = args["x-nv-vqos[0].fec.minRequiredFecPackets"].value_or(0),
      .bitrate_kbps = args["x-nv-video[0].initialBitrateKbps"].value(),

      .client_ip = session.ip};
  session.event_bus->fire_event(immer::box<state::VideoSession>(video));

  // Audio session
  state::AudioSession audio = {.gst_pipeline = session.app.opus_gst_pipeline,

                               .session_id = session.session_id,
                               .event_bus = session.event_bus,

                               .encrypt_audio = static_cast<bool>(args["x-nv-general.featureFlags"].value() & 0x20),
                               .aes_key = session.gcm_key,
                               .aes_iv = session.gcm_iv_key,

                               .port = session.audio_port,
                               .client_ip = session.ip,

                               .packet_duration = args["x-nv-aqos.packetDuration"].value(),
                               .channels = args["x-nv-audio.surround.numChannels"].value(),
                               .mask = args["x-nv-audio.surround.channelMask"].value()};
  session.event_bus->fire_event(immer::box<state::AudioSession>(audio));

  return create_rtsp_msg({options}, 200, "OK", req->sequenceNumber, {});
}

msg_t play(msg_t req) {
  return create_rtsp_msg({{"CSeq", std::to_string(req->sequenceNumber)}}, 200, "OK", req->sequenceNumber, {});
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
  case utils::hash("ANNOUNCE"):
    return announce(std::move(req), session);
  case utils::hash("PLAY"):
    return play(std::move(req));
  default:
    logs::log(logs::warning, "[RTSP] command {} not found", cmd);
    return create_error_msg(404, "NOT FOUND", req->sequenceNumber);
  }
}

} // namespace rtsp::commands