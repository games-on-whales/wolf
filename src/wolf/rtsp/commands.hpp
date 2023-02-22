#pragma once

#include <chrono>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <process/process.hpp>
#include <rtsp/parser.hpp>
#include <state/data-structures.hpp>
#include <string>

namespace rtsp::commands {

using namespace ranges;
using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace rtsp;

RTSP_PACKET error_msg(unsigned short status_code, std::string_view error_msg, int sequence_number = 0) {
  return {.type = RESPONSE,
          .seq_number = sequence_number,
          .response = {.status_code = status_code, .msg = error_msg.data()}};
}

RTSP_PACKET ok_msg(int sequence_number,
                   const std::map<std::string, std::string> &options = {},
                   const std::vector<std::pair<std::string, std::string>> &payloads = {}) {
  return {
      .type = RESPONSE,                              //
      .seq_number = sequence_number,                 //
      .response = {.status_code = 200, .msg = "OK"}, //
      .options = options,                            //
      .payloads = payloads                           //
  };
}

RTSP_PACKET options(const RTSP_PACKET &req) {
  return ok_msg(req.seq_number);
}

RTSP_PACKET describe(const RTSP_PACKET &req, const state::StreamSession &session) {
  auto video_params = "";
  if (session.display_mode.hevc_supported) {
    video_params = "sprop-parameter-sets=AAAAAU";
  }

  std::string audio_speakers = session.audio_mode.speakers                                            //
                               | views::transform([](auto speaker) { return (char)(speaker + '0'); }) //
                               | to<std::string>;                                                     //

  auto audio_params = fmt::format("fmtp:97 surround-params={}{}{}{}",
                                  session.audio_mode.channels,
                                  session.audio_mode.streams,
                                  session.audio_mode.coupled_streams,
                                  audio_speakers);

  return ok_msg(req.seq_number, {}, {{"", video_params}, {"a", audio_params}});
}

RTSP_PACKET setup(const RTSP_PACKET &req, const state::StreamSession &session) {

  int service_port;
  auto type = req.request.stream.type;
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
    return error_msg(404, "NOT FOUND", req.seq_number);
  }

  auto session_opt = "DEADBEEFCAFE;timeout = 90"s;
  return ok_msg(req.seq_number,
                {{"Session", session_opt}, {"Transport", "server_port=" + std::to_string(service_port)}});
}

RTSP_PACKET play(const RTSP_PACKET &req, const state::StreamSession &session) {
  std::this_thread::sleep_for(500ms); // Let's give some time for the gstreamer pipeline to startup
  return ok_msg(req.seq_number);
}

/**
 * Ex given: x-nv-video[0].clientViewportWd:1920
 * returns: <x-nv-video[0].clientViewportWd, 1920>
 */
std::pair<std::string, std::optional<int>> parse_arg_line(std::pair<std::string, std::string> line) {
  auto split = utils::split(line.second, ':');
  std::optional<int> val;
  try {
    val = std::stoi(split[1].data());
  } catch (std::exception const &ex) {
    logs::log(logs::warning, "[RTSP] Unable to parse line: {} error: {}", line, ex.what());
    val = {};
  }
  return std::make_pair(std::string{split[0].data(), split[0].size()}, val);
}

RTSP_PACKET announce(const RTSP_PACKET &req, const state::StreamSession &session) {

  auto args = req.payloads //
              | views::filter([](std::pair<std::string, std::string> line) {
                  return line.first == "a";                    // all args start with a=
                })                                             //
              | views::transform(parse_arg_line)               // turns an arg line into a pair
              | to<std::map<std::string, std::optional<int>>>; // to map

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
  auto csc = args["x-nv-video[0].encoderCscMode"].value();

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
      .slices_per_frame = args["x-nv-video[0].videoEncoderSlicesPerFrame"].value_or(1),

      .color_range = (csc & 0x1) ? state::JPEG : state::MPEG,
      .color_space = state::ColorSpace(csc >> 1),

      .client_ip = session.ip,
      .app_launch_cmd = session.app.run_cmd};
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

  return ok_msg(req.seq_number);
}

RTSP_PACKET message_handler(const RTSP_PACKET &req, const state::StreamSession &session) {
  auto cmd = req.request.cmd;
  logs::log(logs::debug, "[RTSP] received command {}", cmd);

  switch (utils::hash(cmd)) {
  case utils::hash("OPTIONS"):
    return options(req);
  case utils::hash("DESCRIBE"):
    return describe(req, session);
  case utils::hash("SETUP"):
    return setup(req, session);
  case utils::hash("ANNOUNCE"):
    return announce(req, session);
  case utils::hash("PLAY"):
    return play(req, session);
  default:
    logs::log(logs::warning, "[RTSP] command {} not found", cmd);
    return error_msg(404, "NOT FOUND", req.seq_number);
  }
}

} // namespace rtsp::commands