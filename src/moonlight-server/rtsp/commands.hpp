#pragma once

#include "streaming/data-structures.hpp"
#include <chrono>
#include <core/api.hpp>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <rtp/udp-ping.hpp>
#include <rtsp/parser.hpp>
#include <state/data-structures.hpp>
#include <string>

namespace rtsp::commands {

using namespace ranges;
using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace rtsp;
using namespace wolf::core::api;

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

// Additional feature supports
constexpr uint32_t FS_PEN_TOUCH_EVENTS = 0x01;
constexpr uint32_t FS_CONTROLLER_TOUCH_EVENTS = 0x02;

RTSP_PACKET
describe(const RTSP_PACKET &req, const state::StreamSession &session) {
  std::vector<std::pair<std::string, std::string>> payloads;
  if (session.display_mode.hevc_supported) {
    payloads.push_back({"", "sprop-parameter-sets=AAAAAU"});
  }
  if (session.display_mode.av1_supported) {
    payloads.push_back({"a", "a=rtpmap:98 AV1/90000"});
  }

  std::string audio_speakers = session.audio_mode.speakers                                            //
                               | views::transform([](auto speaker) { return (char)(speaker + '0'); }) //
                               | to<std::string>;                                                     //

  payloads.push_back({"a",
                      fmt::format("fmtp:97 surround-params={}{}{}{}",
                                  session.audio_mode.channels,
                                  session.audio_mode.streams,
                                  session.audio_mode.coupled_streams,
                                  audio_speakers)});

  payloads.push_back(
      {"a", fmt::format("x-ss-general.featureFlags: {}", FS_PEN_TOUCH_EVENTS | FS_CONTROLLER_TOUCH_EVENTS)});

  return ok_msg(req.seq_number, {}, payloads);
}

RTSP_PACKET setup(const RTSP_PACKET &req, unsigned short number_of_sessions) {

  int service_port;
  auto type = req.request.stream.type;
  logs::log(logs::trace, "[RTSP] setup type: {}", type);

  switch (utils::hash(type)) {
  case utils::hash("audio"):
    service_port = state::AUDIO_PING_PORT + number_of_sessions;
    break;
  case utils::hash("video"):
    service_port = state::VIDEO_PING_PORT + number_of_sessions;
    break;
  case utils::hash("control"):
    service_port = state::CONTROL_PORT;
    break;
  default:
    return error_msg(404, "NOT FOUND", req.seq_number);
  }

  auto session_opt = "DEADBEEFCAFE;timeout = 90"s;
  return ok_msg(req.seq_number,
                {{"Session", session_opt}, {"Transport", "server_port=" + std::to_string(service_port)}});
}

/**
 * Ex given: x-nv-video[0].clientViewportWd:1920
 * returns: <x-nv-video[0].clientViewportWd, 1920>
 */
std::pair<std::string, std::optional<int>> parse_arg_line(const std::pair<std::string, std::string> &line) {
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

RTSP_PACKET
announce(const RTSP_PACKET &req,
         const state::StreamSession &session,
         std::shared_ptr<dp::event_bus> event_bus,
         unsigned short number_of_sessions) {

  auto args = req.payloads //
              | views::filter([](const std::pair<std::string, std::string> &line) {
                  return line.first == "a";                    // all args start with a=
                })                                             //
              | views::transform(parse_arg_line)               // turns an arg line into a pair
              | to<std::map<std::string, std::optional<int>>>; // to map

  bool video_format_hevc = args["x-nv-vqos[0].bitStreamFormat"].value() == 1;
  bool video_format_av1 = args["x-nv-vqos[0].bitStreamFormat"].value() == 2;
  auto csc = args["x-nv-video[0].encoderCscMode"].value();

  // Video session
  moonlight::DisplayMode display = {.width = args["x-nv-video[0].clientViewportWd"].value(),
                                    .height = args["x-nv-video[0].clientViewportHt"].value(),
                                    .refreshRate = args["x-nv-video[0].maxFPS"].value(),
                                    .hevc_supported = video_format_hevc,
                                    .av1_supported = video_format_av1};

  std::string gst_pipeline;
  if (video_format_av1) {
    logs::log(logs::debug, "[RTSP] Moonlight requested video format AV1");
    gst_pipeline = session.app->av1_gst_pipeline;
  } else if (video_format_hevc) {
    logs::log(logs::debug, "[RTSP] Moonlight requested video format HEVC");
    gst_pipeline = session.app->hevc_gst_pipeline;
  } else {
    logs::log(logs::debug, "[RTSP] Moonlight requested video format H264");
    gst_pipeline = session.app->h264_gst_pipeline;
  }

  unsigned short video_port = state::VIDEO_PING_PORT + number_of_sessions;

  // Video RTP Ping
  rtp::wait_for_ping(video_port, [event_bus](unsigned short client_port, const std::string &client_ip) {
    logs::log(logs::trace, "[PING] video from {}:{}", client_ip, client_port);
    auto ev = state::RTPVideoPingEvent{.client_ip = client_ip, .client_port = client_port};
    event_bus->fire_event(immer::box<state::RTPVideoPingEvent>(ev));
  });

  state::VideoSession video = {
      .display_mode = {.width = display.width, .height = display.height, .refreshRate = display.refreshRate},
      .gst_pipeline = gst_pipeline,

      .session_id = session.session_id,

      .port = video_port,
      .timeout = std::chrono::milliseconds(args["x-nv-video[0].timeoutLengthMs"].value()),
      .packet_size = args["x-nv-video[0].packetSize"].value(),
      .frames_with_invalid_ref_threshold = args["x-nv-video[0].framesWithInvalidRefThreshold"].value(),
      .fec_percentage = 20,
      .min_required_fec_packets = args["x-nv-vqos[0].fec.minRequiredFecPackets"].value_or(0),
      .bitrate_kbps = args["x-nv-video[0].initialBitrateKbps"].value(),
      .slices_per_frame = args["x-nv-video[0].videoEncoderSlicesPerFrame"].value_or(1),

      .color_range = (csc & 0x1) ? state::JPEG : state::MPEG,
      .color_space = state::ColorSpace(csc >> 1),

      .client_ip = session.ip};
  event_bus->fire_event(immer::box<state::VideoSession>(video));

  unsigned short audio_port = state::AUDIO_PING_PORT + number_of_sessions;

  // Audio RTP Ping
  rtp::wait_for_ping(audio_port, [event_bus](unsigned short client_port, const std::string &client_ip) {
    logs::log(logs::trace, "[PING] audio from {}:{}", client_ip, client_port);
    auto ev = state::RTPAudioPingEvent{.client_ip = client_ip, .client_port = client_port};
    event_bus->fire_event(immer::box<state::RTPAudioPingEvent>(ev));
  });

  // Audio session
  state::AudioSession audio = {.gst_pipeline = session.app->opus_gst_pipeline,

                               .session_id = session.session_id,

                               .encrypt_audio = static_cast<bool>(args["x-nv-general.featureFlags"].value() & 0x20),
                               .aes_key = session.aes_key,
                               .aes_iv = session.aes_iv,

                               .port = audio_port,
                               .client_ip = session.ip,

                               .packet_duration = args["x-nv-aqos.packetDuration"].value(),
                               .channels = args["x-nv-audio.surround.numChannels"].value()};
  event_bus->fire_event(immer::box<state::AudioSession>(audio));

  return ok_msg(req.seq_number);
}

RTSP_PACKET
message_handler(const RTSP_PACKET &req,
                const state::StreamSession &session,
                std::shared_ptr<dp::event_bus> event_bus,
                unsigned short number_of_sessions) {
  auto cmd = req.request.cmd;
  logs::log(logs::debug, "[RTSP] received command {}", cmd);

  switch (utils::hash(cmd)) {
  case utils::hash("OPTIONS"):
    return ok_msg(req.seq_number);
  case utils::hash("DESCRIBE"):
    return describe(req, session);
  case utils::hash("SETUP"):
    return setup(req, number_of_sessions);
  case utils::hash("ANNOUNCE"):
    return announce(req, session, event_bus, number_of_sessions);
  case utils::hash("PLAY"):
    return ok_msg(req.seq_number);
  default:
    logs::log(logs::warning, "[RTSP] command {} not found", cmd);
    return error_msg(404, "NOT FOUND", req.seq_number);
  }
}

} // namespace rtsp::commands