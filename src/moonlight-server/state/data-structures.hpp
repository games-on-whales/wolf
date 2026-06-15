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
  // Whether a 10-bit (Main10) encoder pipeline is available for each codec. Advertised to the
  // client as the Main10 video formats; enables 10-bit SDR streaming (less compression banding).
  bool support_hevc_main10 = false;
  bool support_av1_main10 = false;

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
 * Not many clients will actually look at this but the Nintendo Switch will flat out refuse to connect if the
 * advertised display modes don't match
 */
const static immer::array<moonlight::DisplayMode> DISPLAY_CONFIGURATIONS = {{
    // 720p
    {.width = 1280, .height = 720, .refreshRate = 120},
    {.width = 1280, .height = 720, .refreshRate = 60},
    {.width = 1280, .height = 720, .refreshRate = 30},
    // 1080p
    {.width = 1920, .height = 1080, .refreshRate = 120},
    {.width = 1920, .height = 1080, .refreshRate = 60},
    {.width = 1920, .height = 1080, .refreshRate = 30},
    // 1440p
    {.width = 2560, .height = 1440, .refreshRate = 120},
    {.width = 2560, .height = 1440, .refreshRate = 90},
    {.width = 2560, .height = 1440, .refreshRate = 60},
    // 2160p
    {.width = 3840, .height = 2160, .refreshRate = 120},
    {.width = 3840, .height = 2160, .refreshRate = 90},
    {.width = 3840, .height = 2160, .refreshRate = 60},
    // 8k
    {.width = 7680, .height = 4320, .refreshRate = 120},
    {.width = 7680, .height = 4320, .refreshRate = 90},
    {.width = 7680, .height = 4320, .refreshRate = 60},
}};
} // namespace state