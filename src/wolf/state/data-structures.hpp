#pragma once

#include <chrono>
#include <eventbus/event_bus.hpp>
#include <future>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/vector.hpp>
#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>
#include <optional>
#include <streaming/data-structures.hpp>

namespace state {
using namespace std::chrono_literals;

/**
 * All ports are derived from a base port, default: 47989
 */
enum STANDARD_PORTS_MAPPING {
  HTTPS_PORT = 47984,
  HTTP_PORT = 47989,
  VIDEO_STREAM_PORT = 47998,
  CONTROL_PORT = 47999,
  AUDIO_STREAM_PORT = 48000,
  RTSP_SETUP_PORT = 48010
};

struct PairedClient {
  std::string client_id;
  std::string client_cert;

  unsigned short rtsp_port = RTSP_SETUP_PORT;
  unsigned short control_port = CONTROL_PORT;
  unsigned short video_port = VIDEO_STREAM_PORT;
  unsigned short audio_port = AUDIO_STREAM_PORT;
};

struct PairSignal {
  std::string client_ip;
  std::promise<std::string> &user_pin;
};

using PairedClientList = immer::vector<immer::box<PairedClient>>;

/**
 * The stored (and user modifiable) configuration
 */
struct Config {
  std::string uuid;
  std::string hostname;

  /**
   * Mutable, paired_clients will be loaded up on startup
   * but can be added at runtime
   */
  immer::atom<PairedClientList> &paired_clients;

  /**
   * List of available Apps
   */
  immer::vector<moonlight::App> apps;
};

struct AudioMode {

  enum Speakers {
    FRONT_LEFT,
    FRONT_RIGHT,
    FRONT_CENTER,
    LOW_FREQUENCY,
    BACK_LEFT,
    BACK_RIGHT,
    SIDE_LEFT,
    SIDE_RIGHT,
    MAX_SPEAKERS,
  };

  int channels{};
  int streams{};
  int coupled_streams{};
  immer::array<Speakers> speakers;
};

/**
 * Host information like network, certificates and displays
 */
struct Host {
  immer::array<moonlight::DisplayMode> display_modes;
  immer::array<AudioMode> audio_modes;

  const X509 *server_cert;
  const EVP_PKEY *server_pkey;

  std::string external_ip;
  std::string internal_ip;
  std::string mac_address;
};

/**
 * Holds temporary results in order to achieve the multistep pairing process
 */
struct PairCache : PairedClient {
  std::string aes_key;

  // Followings will be filled later on during the pair process
  std::optional<std::string> server_secret;
  std::optional<std::string> server_challenge;
  std::optional<std::string> client_hash;
};

/**
 * The whole application state as a composition of immutable datastructures
 */
struct AppState {
  /**
   * The stored (and user modifiable) configuration
   */
  Config config;

  /**
   * Host information like network, certificates and displays
   */
  Host host;

  /**
   * Mutable temporary results in order to achieve the multistep pairing process
   * It's shared between the two HTTP/HTTPS threads
   */
  immer::atom<immer::map<std::string, PairCache>> &pairing_cache;

  /**
   * A shared bus of events so that we can decouple modules
   */
  std::shared_ptr<dp::event_bus> event_bus;
};

/**
 * A StreamSession is created when a Moonlight user call `launch`
 *
 * This will then be fired up in the event_bus so that the rtsp, command, audio and video threads
 * can start working their magic.
 */
struct StreamSession {
  // A unique ID that identifies this session
  std::size_t session_id;

  std::shared_ptr<dp::event_bus> event_bus;

  moonlight::DisplayMode display_mode;
  AudioMode audio_mode;

  std::string app_id;

  std::string gcm_key;
  std::string gcm_iv_key;

  std::string unique_id;
  std::string ip;

  std::uint16_t rtsp_port{};
  std::uint16_t control_port{};
  std::uint16_t audio_port{};
  std::uint16_t video_port{};
};

/**
 * A ControlSessions is created after the param exchange over RTSP
 */
struct ControlSession {
  // A unique ID that identifies this session
  std::size_t session_id;

  std::shared_ptr<dp::event_bus> event_bus;

  std::uint16_t port;
  std::size_t peers;

  int protocol_type;

  std::string gcm_key;
  std::string gcm_iv;

  std::chrono::milliseconds timeout = 150ms;
  std::string host = "0.0.0.0";
};

} // namespace state