#pragma once

#include "gst-video-context.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <core/audio.hpp>
#include <core/input.hpp>
#include <core/virtual-display.hpp>
#include <deque>
#include <eventbus/event_bus.hpp>
#include <events/events.hpp>
#include <helpers/tsqueue.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/vector.hpp>
#include <moonlight/control.hpp>
#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>
#include <optional>
#include <state/serialised_config.hpp>
#include <utility>
#include <vector>

namespace state {
using namespace std::chrono_literals;
using namespace wolf::core;
namespace ba = boost::asio;

/**
 * All ports are derived from a base port, default: 47989
 */
enum STANDARD_PORTS_MAPPING {
  HTTPS_PORT = 47984,
  HTTP_PORT = 47989,
  CONTROL_PORT = 47999,
  VIDEO_PING_PORT = 48100,
  AUDIO_PING_PORT = 48200,
  RTSP_SETUP_PORT = 48010
};

inline int get_port(STANDARD_PORTS_MAPPING port) {
  switch (port) {
  case HTTPS_PORT:
    return utils::get_env("WOLF_HTTPS_PORT") ? std::stoi(utils::get_env("WOLF_HTTPS_PORT")) : HTTPS_PORT;
  case HTTP_PORT:
    return utils::get_env("WOLF_HTTP_PORT") ? std::stoi(utils::get_env("WOLF_HTTP_PORT")) : HTTP_PORT;
  case CONTROL_PORT:
    return utils::get_env("WOLF_CONTROL_PORT") ? std::stoi(utils::get_env("WOLF_CONTROL_PORT")) : CONTROL_PORT;
  case VIDEO_PING_PORT:
    return utils::get_env("WOLF_VIDEO_PING_PORT") ? std::stoi(utils::get_env("WOLF_VIDEO_PING_PORT")) : VIDEO_PING_PORT;
  case AUDIO_PING_PORT:
    return utils::get_env("WOLF_AUDIO_PING_PORT") ? std::stoi(utils::get_env("WOLF_AUDIO_PING_PORT")) : AUDIO_PING_PORT;
  case RTSP_SETUP_PORT:
    return utils::get_env("WOLF_RTSP_SETUP_PORT") ? std::stoi(utils::get_env("WOLF_RTSP_SETUP_PORT")) : RTSP_SETUP_PORT;
  }
  return -1;
}

using PairedClientList = immer::vector<immer::box<wolf::config::PairedClient>>;
using ProfilesList = immer::vector<immer::box<events::Profile>>;

enum Encoder {
  NVIDIA,
  VAAPI,
  QUICKSYNC,
  SOFTWARE,
  APPLE,
  UNKNOWN
};

/**
 * The stored (and user modifiable) configuration
 */
struct Config {
  std::string uuid;
  std::string hostname;
  std::string config_source;
  bool support_hevc;
  bool support_av1;

  /**
   * Mutable, paired_clients will be loaded up on startup
   * but can be added at runtime
   */
  std::shared_ptr<immer::atom<PairedClientList>> paired_clients;

  /**
   * List of available Profiles,
   * each profile contains a list of apps.
   * Profiles will be shown in WolfUI
   */
  std::shared_ptr<immer::atom<ProfilesList>> profiles;
};

/**
 * Host information like network, certificates and displays
 */
struct Host {
  immer::array<moonlight::DisplayMode> display_modes;
  immer::array<audio::AudioMode> audio_modes;

  x509::x509_ptr server_cert;
  x509::pkey_ptr server_pkey;

  // Network information can be manually set by users, if not, we'll automatically gather them
  std::optional<std::string> internal_ip;
  std::optional<std::string> mac_address;

  /**
   * The base path on the host where we are allowed to store data
   */
  std::string host_base_state_folder;

  /**
   * The path in the current Wolf context (probably a container) where we are allowed to store data
   */
  std::string local_base_state_folder;

  /**
   * The path (or usually the Docker volume) on the host where we'll put the audio/video sockets
   */
  std::string host_xdg_runtime_dir;
};

enum class PAIR_PHASE {
  NONE,
  GETSERVERCERT,
  CLIENTCHALLENGE,
  SERVERCHALLENGERESP,
  CLIENTPAIRINGSECRET
};

/**
 * Holds temporary results in order to achieve the multistep pairing process
 */
struct PairCache {
  std::string client_cert;
  std::string aes_key;

  // Followings will be filled later on during the pair process
  std::optional<std::string> server_secret;
  std::optional<std::string> server_challenge;
  std::optional<std::string> client_hash;

  /**
   * @brief used as a security measure to prevent out of order calls
   */
  PAIR_PHASE last_phase = PAIR_PHASE::NONE;
};

using SessionsAtoms = std::shared_ptr<immer::atom<immer::vector<events::StreamSession>>>;

/**
 * The whole application state as a composition of immutable datastructures
 */
struct AppState {
  /**
   * The stored (and user modifiable) configuration
   */
  immer::box<Config> config;

  /**
   * Host information like network, certificates and displays
   */
  immer::box<Host> host;

  /**
   * Mutable, temporary results in order to achieve the multistep pairing process
   * It's shared between the two HTTP/HTTPS threads
   */
  std::shared_ptr<immer::atom<immer::map<std::string, PairCache>>> pairing_cache;

  /**
   * Mutable, temporary promises to be resolved when the client sends the correct pin
   */
  std::shared_ptr<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>> pairing_atom;

  /**
   * A shared bus of events so that we can decouple modules
   */
  std::shared_ptr<events::EventBusType> event_bus;

  std::shared_ptr<immer::atom<immer::vector<events::Lobby>>> lobbies;

  /**
   * A single global Gstreamer video context shared with all the pipelines
   */
  std::shared_ptr<immer::atom<gst_video_context::gst_context_ptr>> gst_context =
      std::make_shared<immer::atom<gst_video_context::gst_context_ptr>>();

  /**
   * A list of all currently running (and paused) streaming sessions
   */
  SessionsAtoms running_sessions;
};

const static immer::array<audio::AudioMode> AUDIO_CONFIGURATIONS = {
    // TODO: opusenc doesn't allow us to set `coupled_streams` and `streams`
    //       don't change these or Moonlight will not be able to decode audio
    //       https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/1.24.6/subprojects/gst-plugins-base/ext/opus/gstopusenc.c#L661-666
    {// Stereo
     {.channels = 2,
      .streams = 1,
      .coupled_streams = 1,
      .speakers = {audio::AudioMode::Speakers::FRONT_LEFT, audio::AudioMode::Speakers::FRONT_RIGHT},
      .bitrate = 96000},
     // 5.1
     {.channels = 6,
      .streams = 4,
      .coupled_streams = 2,
      .speakers = {audio::AudioMode::Speakers::FRONT_LEFT,
                   audio::AudioMode::Speakers::FRONT_RIGHT,
                   audio::AudioMode::Speakers::FRONT_CENTER,
                   audio::AudioMode::Speakers::LOW_FREQUENCY,
                   audio::AudioMode::Speakers::BACK_LEFT,
                   audio::AudioMode::Speakers::BACK_RIGHT},
      .bitrate = 256000},
     // 7.1
     {.channels = 8,
      .streams = 5,
      .coupled_streams = 3,
      .speakers = {audio::AudioMode::Speakers::FRONT_LEFT,
                   audio::AudioMode::Speakers::FRONT_RIGHT,
                   audio::AudioMode::Speakers::FRONT_CENTER,
                   audio::AudioMode::Speakers::LOW_FREQUENCY,
                   audio::AudioMode::Speakers::BACK_LEFT,
                   audio::AudioMode::Speakers::BACK_RIGHT,
                   audio::AudioMode::Speakers::SIDE_LEFT,
                   audio::AudioMode::Speakers::SIDE_RIGHT},
      .bitrate = 450000}}};

static const audio::AudioMode &get_audio_mode(int channels, bool high_quality) {
  int base_index = 0;
  if (channels == 6) {
    base_index = 1;
  } else if (channels == 8) {
    base_index = 2;
  } else if (channels != 2) {
    logs::log(logs::warning, "Moonlight requested an impossible number of channels: {}", channels);
  }

  return AUDIO_CONFIGURATIONS[base_index]; // TODO: add high quality settings, it sounds bad if we can't change the
                                           //       opusenc settings too..
}

/**
 * Advertised display modes. Moonlight clients build their resolution dropdown
 * (and gate their "custom resolution" field) from this list, so any monitor
 * mode a user might want to stream at has to appear here — including ultrawide
 * (21:9, 32:9) and common 16:10 / 3:2 laptop panels.
 *
 * Strict clients like Nintendo Switch refuse to connect if their native mode
 * isn't advertised, so the catalog errs on the side of inclusive.
 */
const static immer::array<moonlight::DisplayMode> DISPLAY_CONFIGURATIONS = []() {
  struct Res {
    int w, h;
  };
  // Heights <= 1440 also get high-refresh rates (144/165/240);
  // higher resolutions stay on 30/60/90/120 to keep the list sane.
  const std::vector<Res> lo_res = {
      // 16:9
      {1280, 720},
      {1600, 900},
      {1920, 1080},
      {2560, 1440},
      // 16:10
      {1280, 800},
      {1440, 900},
      {1680, 1050},
      {1920, 1200},
      // 21:9 ultrawide
      {2560, 1080},
      {3440, 1440},
      // 32:9 super-ultrawide
      {3840, 1080},
      {5120, 1440},
      // 4:3 / 5:4 legacy
      {1024, 768},
      {1280, 960},
      {1280, 1024},
      {1600, 1200},
  };
  const std::vector<Res> hi_res = {
      // 16:9
      {3200, 1800},
      {3840, 2160},
      {5120, 2880},
      {7680, 4320},
      // 16:10
      {2560, 1600},
      {2880, 1800},
      {3840, 2400},
      // 3:2 (Surface / Framework 13 / MBP-style)
      {2160, 1440},
      {2256, 1504},
      {2560, 1700},
      {3000, 2000},
      {3240, 2160},
      // 21:9 ultrawide high-res
      {3840, 1600},
      {5120, 2160},
      // 32:9 super-ultrawide high-res
      {7680, 2160},
      // 4:3 high-res
      {2048, 1536},
  };

  const std::vector<int> base_rates = {120, 90, 60, 30};
  const std::vector<int> extra_hi_refresh = {240, 165, 144};

  std::vector<moonlight::DisplayMode> modes;
  modes.reserve((lo_res.size() * (base_rates.size() + extra_hi_refresh.size())) + (hi_res.size() * base_rates.size()));
  for (const auto &r : lo_res) {
    for (int rr : extra_hi_refresh)
      modes.push_back({.width = r.w, .height = r.h, .refreshRate = rr});
    for (int rr : base_rates)
      modes.push_back({.width = r.w, .height = r.h, .refreshRate = rr});
  }
  for (const auto &r : hi_res) {
    for (int rr : base_rates)
      modes.push_back({.width = r.w, .height = r.h, .refreshRate = rr});
  }
  return immer::array<moonlight::DisplayMode>(modes.begin(), modes.end());
}();
} // namespace state