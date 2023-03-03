#pragma once

#include <audio/audio.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <eventbus/event_bus.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/vector.hpp>
#include <input/input.hpp>
#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>
#include <optional>
#include <streaming/data-structures.hpp>

namespace state {
using namespace std::chrono_literals;
namespace ba = boost::asio;

/**
 * All ports are derived from a base port, default: 47989
 */
enum STANDARD_PORTS_MAPPING {
  HTTPS_PORT = 47984,
  HTTP_PORT = 47989,
  CONTROL_PORT = 47999,
  VIDEO_PING_PORT = 47998,
  AUDIO_PING_PORT = 48000,
  RTSP_SETUP_PORT = 48010
};

struct PairedClient {
  std::string client_id;
  std::string client_cert;
};

struct PairSignal {
  std::string client_ip;
  std::shared_ptr<boost::promise<std::string>> user_pin;
};

using PairedClientList = immer::vector<immer::box<PairedClient>>;

namespace gstreamer {
namespace video {
constexpr std::string_view DEFAULT_SOURCE =
    "videotestsrc pattern=ball flip=true is-live=true ! video/x-raw, framerate={fps}/1";
constexpr std::string_view DEFAULT_PARAMS = "videoscale ! videoconvert ! "
                                            "video/x-raw, width={width}, height={height}, "
                                            "chroma-site={color_range}, colorimetry={color_space}, format=NV12";

constexpr std::string_view DEFAULT_H264_ENCODER =
    "encodebin "
    " profile=\"video/x-h264, "
    " profile=main, tune=zerolatency, bframes=0, aud=false, stream-format=byte-stream, bitrate={bitrate}, "
    " insert-vui=false \"";

constexpr std::string_view DEFAULT_H265_ENCODER =
    "encodebin "
    " profile=\"video/x-h265, "
    " profile=main, tune=zerolatency, bframes=0, aud=false, stream-format=byte-stream, bitrate={bitrate}, "
    " insert-vui=false\"";

constexpr std::string_view DEFAULT_SINK =
    "rtpmoonlightpay_video name=moonlight_pay payload_size={payload_size} fec_percentage={fec_percentage} "
    "min_required_fec_packets={min_required_fec_packets}"
    " ! "
    "udpsink host={client_ip} port={client_port} sync=false";
} // namespace video

namespace audio {
constexpr std::string_view DEFAULT_SOURCE = "audiotestsrc wave=ticks is-live=true";
constexpr std::string_view DEFAULT_PARAMS = "audio/x-raw, channels={channels}";
constexpr std::string_view DEFAULT_OPUS_ENCODER =
    "opusenc bitrate={bitrate} bitrate-type=cbr frame-size={packet_duration} "
    "bandwidth=fullband audio-type=generic max-payload-size=1400";
constexpr std::string_view DEFAULT_SINK = "rtpmoonlightpay_audio name=moonlight_pay "
                                          "packet_duration={packet_duration} "
                                          "encrypt={encrypt} aes_key=\"{aes_key}\" aes_iv=\"{aes_iv}\" "
                                          " ! "
                                          "udpsink host={client_ip} port={client_port} sync=false";
} // namespace audio

} // namespace gstreamer

struct App {
  moonlight::App base;

  std::string h264_gst_pipeline;
  std::string hevc_gst_pipeline;
  std::string opus_gst_pipeline;

  std::string run_cmd;
};

/**
 * The stored (and user modifiable) configuration
 */
struct Config {
  std::string uuid;
  std::string hostname;
  std::string config_source;
  bool support_hevc;

  /**
   * Mutable, paired_clients will be loaded up on startup
   * but can be added at runtime
   */
  immer::atom<PairedClientList> &paired_clients;

  /**
   * List of available Apps
   */
  immer::vector<App> apps;
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
 * A StreamSession is created when a Moonlight user call `launch`
 *
 * This will then be fired up in the event_bus so that the rtsp, command, audio and video threads
 * can start working their magic.
 */
struct StreamSession {
  moonlight::DisplayMode display_mode;
  AudioMode audio_mode;
  input::InputReady virtual_inputs;

  App app;
  // gcm encryption keys
  std::string aes_key;
  std::string aes_iv;
  // client info
  std::size_t session_id;
  std::string ip;
};

using SessionsAtoms = std::shared_ptr<immer::atom<immer::vector<StreamSession>>>;

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

  /**
   * A thread pool, used to start all kind of concurrent operations
   */
  std::shared_ptr<ba::thread_pool> t_pool;
};

} // namespace state