#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <core/audio.hpp>
#include <core/input.hpp>
#include <core/virtual-display.hpp>
#include <deque>
#include <eventbus/event_bus.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/vector.hpp>
#include <moonlight/control.hpp>
#include <moonlight/data-structures.hpp>
#include <openssl/x509.h>
#include <optional>
#include <helpers/tsqueue.hpp>
#include <toml.hpp>
#include <utility>


namespace state {
using namespace std::chrono_literals;
using namespace wolf::core;
namespace ba = boost::asio;

struct PlugDeviceEvent {
  std::size_t session_id;
  std::vector<std::map<std::string, std::string>> udev_events;
  std::vector<std::pair<std::string, std::vector<std::string>>> udev_hw_db_entries;
};

struct UnplugDeviceEvent {
  std::size_t session_id;
  std::vector<std::map<std::string, std::string>> udev_events;
  std::vector<std::pair<std::string, std::vector<std::string>>> udev_hw_db_entries;
};

using devices_atom_queue = TSQueue<immer::box<PlugDeviceEvent>>;

struct Runner {

  virtual void run(std::size_t session_id,
                   std::string_view app_state_folder,
                   std::shared_ptr<devices_atom_queue> plugged_devices_queue,
                   const immer::array<std::string> &virtual_inputs,
                   const immer::array<std::pair<std::string, std::string>> &paths,
                   const immer::map<std::string, std::string> &env_variables) = 0;

  virtual toml::value serialise() = 0;
};

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

struct PairSignal {
  std::string client_ip;
  std::string host_ip;
  std::shared_ptr<boost::promise<std::string>> user_pin;
};

struct RTPVideoPingEvent {
  std::string client_ip;
  unsigned short client_port;
};

struct RTPAudioPingEvent {
  std::string client_ip;
  unsigned short client_port;
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

struct App {
  moonlight::App base;

  std::string h264_gst_pipeline;
  Encoder h264_encoder;
  std::string hevc_gst_pipeline;
  Encoder hevc_encoder;
  std::string av1_gst_pipeline;
  Encoder av1_encoder;

  std::string render_node;

  std::string opus_gst_pipeline;
  bool start_virtual_compositor;
  std::shared_ptr<Runner> runner;
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

using JoypadTypes = std::variant<input::XboxOneJoypad, input::SwitchJoypad, input::PS5Joypad>;
using JoypadList = immer::map<int /* controller number */, std::shared_ptr<JoypadTypes>>;

/**
 * A StreamSession is created when a Moonlight user call `launch`
 *
 * This will then be fired up in the event_bus so that the rtsp, command, audio and video threads
 * can start working their magic.
 */
struct StreamSession {
  moonlight::DisplayMode display_mode;
  AudioMode audio_mode;

  std::shared_ptr<dp::event_bus> event_bus;
  std::shared_ptr<App> app;
  std::string app_state_folder;

  // gcm encryption keys
  std::string aes_key;
  std::string aes_iv;

  // client info
  std::size_t session_id;
  std::string ip;

  /**
   * Optional: the wayland display for the current session.
   * Will be only set during an active streaming and destroyed on stream end.
   */
  std::shared_ptr<immer::atom<virtual_display::wl_state_ptr>> wayland_display =
      std::make_shared<immer::atom<virtual_display::wl_state_ptr>>();

  // virtual devices
  std::shared_ptr<input::Mouse> mouse;
  std::shared_ptr<input::Keyboard> keyboard;
  std::shared_ptr<immer::atom<JoypadList>> joypads;
  std::shared_ptr<input::PenTablet> pen_tablet = nullptr;     /* Optional, will be set on first use*/
  std::shared_ptr<input::TouchScreen> touch_screen = nullptr; /* Optional, will be set on first use*/
};

// TODO: unplug device event? Or should this be tied to the session?

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
};

} // namespace state