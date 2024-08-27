#pragma once

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
#include <toml.hpp>
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

struct PairedClient {
  std::string client_cert;
  std::string app_state_folder;
  uint run_uid = 1000;
  uint run_gid = 1000;
};

using PairedClientList = immer::vector<immer::box<PairedClient>>;

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
   * List of available Apps
   */
  immer::vector<events::App> apps;
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
   * Mutable temporary results in order to achieve the multistep pairing process
   * It's shared between the two HTTP/HTTPS threads
   */
  std::shared_ptr<immer::atom<immer::map<std::string, PairCache>>> pairing_cache;

  /**
   * A shared bus of events so that we can decouple modules
   */
  std::shared_ptr<dp::event_bus> event_bus;

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
      .speakers = {audio::AudioMode::FRONT_LEFT, audio::AudioMode::FRONT_RIGHT},
      .bitrate = 96000},
     // 5.1
     {.channels = 6,
      .streams = 4,
      .coupled_streams = 2,
      .speakers = {audio::AudioMode::FRONT_LEFT,
                   audio::AudioMode::FRONT_RIGHT,
                   audio::AudioMode::FRONT_CENTER,
                   audio::AudioMode::LOW_FREQUENCY,
                   audio::AudioMode::BACK_LEFT,
                   audio::AudioMode::BACK_RIGHT},
      .bitrate = 256000},
     // 7.1
     {.channels = 8,
      .streams = 5,
      .coupled_streams = 3,
      .speakers = {audio::AudioMode::FRONT_LEFT,
                   audio::AudioMode::FRONT_RIGHT,
                   audio::AudioMode::FRONT_CENTER,
                   audio::AudioMode::LOW_FREQUENCY,
                   audio::AudioMode::BACK_LEFT,
                   audio::AudioMode::BACK_RIGHT,
                   audio::AudioMode::SIDE_LEFT,
                   audio::AudioMode::SIDE_RIGHT},
      .bitrate = 450000}}};

static const audio::AudioMode &get_audio_mode(int channels, bool high_quality) {
  int base_index = 0;
  if (channels == 6) {
    base_index = 2;
  } else if (channels == 8) {
    base_index = 4;
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