#pragma once

#include <chrono>
#include <events/events.hpp>
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
using namespace wolf::core;

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
using namespace wolf::core::audio;

RTSP_PACKET
describe(const RTSP_PACKET &req, const events::StreamSession &session) {
  std::vector<std::pair<std::string, std::string>> payloads;
  if (session.display_mode.hevc_supported) {
    payloads.push_back({"", "sprop-parameter-sets=AAAAAU"});
  }
  if (session.display_mode.av1_supported) {
    payloads.push_back({"a", "a=rtpmap:98 AV1/90000"});
  }

  // Advertise all audio configurations
  for (const auto audio_mode : state::AUDIO_CONFIGURATIONS) {
    auto mapping_p = audio_mode.speakers;
    // Opusenc forces a re-mapping to Vorbis; see
    // https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/1.24.6/subprojects/gst-plugins-base/ext/opus/gstopusenc.c#L549-572
    if (audio_mode.channels == 6) { // 5.1
      mapping_p = {
          // The mapping for 5.1 is: [0 1 4 5 2 3]
          AudioMode::FRONT_LEFT,
          AudioMode::FRONT_RIGHT,
          AudioMode::BACK_LEFT,
          AudioMode::BACK_RIGHT,
          AudioMode::FRONT_CENTER,
          AudioMode::LOW_FREQUENCY,
      };
    } else if (audio_mode.channels == 8) { // 7.1
      mapping_p = {
          // The mapping for 7.1 is: [0 1 4 5 2 3 6 7]
          AudioMode::FRONT_LEFT,
          AudioMode::FRONT_RIGHT,
          AudioMode::BACK_LEFT,
          AudioMode::BACK_RIGHT,
          AudioMode::FRONT_CENTER,
          AudioMode::LOW_FREQUENCY,
          AudioMode::SIDE_LEFT,
          AudioMode::SIDE_RIGHT,
      };
    }

    /**
     * GFE advertises incorrect mapping for normal quality configurations,
     * as a result, Moonlight rotates all channels from index '3' to the right
     * To work around this, rotate channels to the left from index '3'
     */
    if (audio_mode.channels > 2) { // 5.1 and 7.1
      std::rotate(mapping_p.begin() + 3, mapping_p.begin() + 4, mapping_p.end());
    }
    std::string audio_speakers = mapping_p                                                              //
                                 | views::transform([](auto speaker) { return (char)(speaker + '0'); }) //
                                 | to<std::string>;
    auto surround_params = fmt::format("fmtp:97 surround-params={}{}{}{}",
                                       audio_mode.channels,
                                       audio_mode.streams,
                                       audio_mode.coupled_streams,
                                       audio_speakers);

    payloads.push_back({"a", surround_params});
    logs::log(logs::trace, "[RTSP] Sending audio surround params: {}", surround_params);
  }

  payloads.push_back(
      {"a", fmt::format("x-ss-general.featureFlags: {}", FS_PEN_TOUCH_EVENTS | FS_CONTROLLER_TOUCH_EVENTS)});

  return ok_msg(req.seq_number, {}, payloads);
}

RTSP_PACKET setup(const RTSP_PACKET &req, const state::StreamSession &session) {

  int service_port;
  auto type = req.request.stream.type;
  logs::log(logs::trace, "[RTSP] setup type: {}", type);

  switch (utils::hash(type)) {
  case utils::hash("audio"):
    service_port = session.audio_stream_port;
    break;
  case utils::hash("video"):
    service_port = session.video_stream_port;
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
    if (split.size() == 2 && split[1].data()) {
      val = std::stoi(split[1].data());
    } else {
      logs::log(logs::warning, "[RTSP] Received unparsable value {}", line);
    }
  } catch (std::exception const &ex) {
    logs::log(logs::warning, "[RTSP] Unable to parse line: {} error: {}", line, ex.what());
    val = {};
  }
  return std::make_pair(std::string{split[0].data(), split[0].size()}, val);
}

RTSP_PACKET
announce(const RTSP_PACKET &req, const state::StreamSession &session) {

  auto args = req.payloads //
              | views::filter([](const std::pair<std::string, std::string> &line) {
                  return line.first == "a";                    // all args start with a=
                })                                             //
              | views::transform(parse_arg_line)               // turns an arg line into a pair
              | to<std::map<std::string, std::optional<int>>>; // to map

  bool video_format_hevc = args["x-nv-vqos[0].bitStreamFormat"].value_or(0) == 1;
  bool video_format_av1 = args["x-nv-vqos[0].bitStreamFormat"].value_or(0) == 2;
  auto csc = args["x-nv-video[0].encoderCscMode"].value_or(0);

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

  auto audio_channels = args["x-nv-audio.surround.numChannels"].value_or(session.audio_channel_count);
  auto fec_percentage = 20; // TODO: setting?

  long bitrate = args["x-nv-vqos[0].bw.maximumBitrateKbps"].value_or(15500);
  // If the client sent a configured bitrate adjust it (Moonlight extension)
  if (auto configured_bitrate = args["x-ml-video.configuredBitrateKbps"]; configured_bitrate.has_value()) {
    bitrate = *configured_bitrate;

    // If the FEC percentage isn't too high, adjust the configured bitrate to ensure video
    // traffic doesn't exceed the user's selected bitrate when the FEC shards are included.
    if (fec_percentage <= 80) {
      bitrate /= 100.f / (100 - fec_percentage);
    }

    // Adjust the bitrate to account for audio traffic bandwidth usage (capped at 20% reduction).
    // The bitrate per channel is 256 Kbps for high quality mode and 96 Kbps for normal quality.
    auto audioBitrateAdjustment = 96 * audio_channels;
    bitrate -= std::min((std::int64_t)audioBitrateAdjustment, bitrate / 5);

    // Reduce it by another 500Kbps to account for A/V packet overhead and control data
    // traffic (capped at 10% reduction).
    bitrate -= std::min((std::int64_t)500, bitrate / 10);
    logs::log(logs::debug, "[RTSP] Adjusted video bitrate to {} Kbps", bitrate);
  }

  // Video session
  unsigned short video_port = state::VIDEO_PING_PORT + number_of_sessions;
  state::VideoSession video = {
      .display_mode = {.width = display.width, .height = display.height, .refreshRate = display.refreshRate},
      .gst_pipeline = gst_pipeline,

      .session_id = session.session_id,

      .port = session.video_stream_port,
      .timeout = std::chrono::milliseconds(args["x-nv-video[0].timeoutLengthMs"].value_or(7000)),
      .packet_size = args["x-nv-video[0].packetSize"].value_or(1024),
      .frames_with_invalid_ref_threshold = args["x-nv-video[0].framesWithInvalidRefThreshold"].value_or(0),
      .fec_percentage = fec_percentage,
      .min_required_fec_packets = args["x-nv-vqos[0].fec.minRequiredFecPackets"].value_or(0),
      .bitrate_kbps = bitrate,
      .slices_per_frame = args["x-nv-video[0].videoEncoderSlicesPerFrame"].value_or(1),

      .color_range = (csc & 0x1) ? events::JPEG : events::MPEG,
      .color_space = events::ColorSpace(csc >> 1),

      .client_ip = session.ip};
  event_bus->fire_event(immer::box<state::VideoSession>(video));

  // Audio session
  auto high_quality_audio = args["x-nv-audio.surround.AudioQuality"].value_or(0) == 1;
  auto audio_mode = state::get_audio_mode(audio_channels, high_quality_audio);
  events::AudioSession audio = {
      .gst_pipeline = session.app->opus_gst_pipeline,

      .session_id = session.session_id,

      .encrypt_audio = static_cast<bool>(args["x-nv-general.featureFlags"].value_or(167) & 0x20),
      .aes_key = session.aes_key,
      .aes_iv = session.aes_iv,

      .port = session.audio_stream_port,
      .client_ip = session.ip,

      .packet_duration = args["x-nv-aqos.packetDuration"].value_or(5),
      .audio_mode = audio_mode};
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
    return setup(req, session);
  case utils::hash("ANNOUNCE"):
    return announce(req, session);
  case utils::hash("PLAY"):
    return ok_msg(req.seq_number);
  default:
    logs::log(logs::warning, "[RTSP] command {} not found", cmd);
    return error_msg(404, "NOT FOUND", req.seq_number);
  }
}

} // namespace rtsp::commands