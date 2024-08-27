#pragma once

#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_FUTURE
#include <boost/thread.hpp>
#include <boost/thread/future.hpp>
#include <core/audio.hpp>
#include <core/input.hpp>
#include <core/virtual-display.hpp>
#include <cstddef>
#include <eventbus/event_bus.hpp>
#include <helpers/tsqueue.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/vector.hpp>
#include <moonlight/control.hpp>
#include <moonlight/data-structures.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string_view>
#include <toml.hpp>

namespace wolf::core::events {

struct PairSignal {
  std::string client_ip;
  std::string host_ip;
  std::shared_ptr<boost::promise<std::string>> user_pin;
};

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

using devices_atom_queue = TSQueue<immer::box<events::PlugDeviceEvent>>;

struct Runner {

  virtual void run(std::size_t session_id,
                   std::string_view app_state_folder,
                   std::shared_ptr<devices_atom_queue> plugged_devices_queue,
                   const immer::array<std::string> &virtual_inputs,
                   const immer::array<std::pair<std::string, std::string>> &paths,
                   const immer::map<std::string, std::string> &env_variables,
                   std::string_view render_node) = 0;

  virtual toml::value serialise() = 0;
};

struct App {
  moonlight::App base;

  std::string h264_gst_pipeline;
  std::string hevc_gst_pipeline;
  std::string av1_gst_pipeline;

  std::string render_node;

  std::string opus_gst_pipeline;
  bool start_virtual_compositor;
  std::shared_ptr<Runner> runner;
  moonlight::control::pkts::CONTROLLER_TYPE joypad_type;
};

using MouseTypes = std::variant<input::Mouse, virtual_display::WaylandMouse>;
using KeyboardTypes = std::variant<input::Keyboard, virtual_display::WaylandKeyboard>;
using JoypadTypes = std::variant<input::XboxOneJoypad, input::SwitchJoypad, input::PS5Joypad>;
using JoypadList = immer::map<int /* controller number */, std::shared_ptr<JoypadTypes>>;

enum class ColorRange {
  JPEG,
  MPEG
};

enum class ColorSpace : int {
  BT601,
  BT709,
  BT2020
};

/**
 * A VideoSession is created after the param exchange over RTSP
 */
struct VideoSession {
  wolf::core::virtual_display::DisplayMode display_mode;
  std::string gst_pipeline;

  // A unique ID that identifies this session
  std::size_t session_id;

  std::uint16_t port;
  std::chrono::milliseconds timeout;

  int packet_size;
  int frames_with_invalid_ref_threshold;
  int fec_percentage;
  int min_required_fec_packets;
  long bitrate_kbps;
  int slices_per_frame;

  ColorRange color_range;
  ColorSpace color_space;

  std::string client_ip;
};

struct AudioSession {
  std::string gst_pipeline;

  // A unique ID that identifies this session
  std::size_t session_id;

  bool encrypt_audio;
  std::string aes_key;
  std::string aes_iv;

  std::uint16_t port;
  std::string client_ip;

  int packet_duration;
  wolf::core::audio::AudioMode audio_mode;
};

/**
 * Events received in the ENET Control Session
 * TODO: break this down into more meaningful events
 */
struct ControlEvent {
  // A unique ID that identifies this session
  std::size_t session_id;

  moonlight::control::pkts::PACKET_TYPE type;
  std::string_view raw_packet;
};

struct PauseStreamEvent {
  std::size_t session_id;
};

struct ResumeStreamEvent {
  std::size_t session_id;
};

struct StopStreamEvent {
  std::size_t session_id;
};

struct RTPVideoPingEvent {
  std::string client_ip;
  unsigned short client_port;
};

struct RTPAudioPingEvent {
  std::string client_ip;
  unsigned short client_port;
};

struct StreamSession;

using EventTypes = std::variant<PlugDeviceEvent,
                                PairSignal,
                                UnplugDeviceEvent,
                                StreamSession,
                                VideoSession,
                                AudioSession,
                                ControlEvent,
                                PauseStreamEvent,
                                ResumeStreamEvent,
                                StopStreamEvent,
                                RTPVideoPingEvent,
                                RTPAudioPingEvent>;

/**
 * A StreamSession is created when a Moonlight user call `launch`
 *
 * This will then be fired up in the event_bus so that the rtsp, command, audio and video threads
 * can start working their magic.
 */
struct StreamSession {
  moonlight::DisplayMode display_mode;
  int audio_channel_count;

  std::shared_ptr<dp::event_bus<EventTypes>> event_bus;
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
  std::shared_ptr<std::optional<MouseTypes>> mouse = std::make_shared<std::optional<MouseTypes>>();
  std::shared_ptr<std::optional<KeyboardTypes>> keyboard = std::make_shared<std::optional<KeyboardTypes>>();

  std::shared_ptr<immer::atom<JoypadList>> joypads = std::make_shared<immer::atom<JoypadList>>();

  std::shared_ptr<std::optional<input::PenTablet>> pen_tablet =
      std::make_shared<std::optional<input::PenTablet>>(); /* Optional, will be set on first use */
  std::shared_ptr<std::optional<input::TouchScreen>> touch_screen =
      std::make_shared<std::optional<input::TouchScreen>>(); /* Optional, will be set on first use */
};

} // namespace wolf::core::events