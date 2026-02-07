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
#include <state/serialised_config.hpp>
#include <string_view>

namespace wolf::core::events {

struct PairSignal {
  std::string client_ip;
  std::string host_ip;
  std::shared_ptr<boost::promise<std::string>> user_pin;
};

struct PlugDeviceEvent {
  std::string session_id;
  std::vector<std::map<std::string, std::string>> udev_events;
  std::vector<std::pair<std::string, std::vector<std::string>>> udev_hw_db_entries;
};

struct UnplugDeviceEvent {
  std::string session_id;
  std::vector<std::map<std::string, std::string>> udev_events;
  std::vector<std::pair<std::string, std::vector<std::string>>> udev_hw_db_entries;
};

using devices_atom_queue = TSQueue<immer::box<events::PlugDeviceEvent>>;
using RunnerTypes = rfl::TaggedUnion<"type", wolf::config::AppCMD, wolf::config::AppDocker>;

struct Runner {
  virtual ~Runner() = default;

  virtual void run(std::string_view session_id,
                   std::string_view app_state_folder,
                   std::string_view host_xdg_runtime_dir,
                   std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
                   const immer::array<std::string> &virtual_inputs,
                   const immer::array<std::pair<std::string, std::string>> &paths,
                   const immer::map<std::string, std::string> &env_variables,
                   std::string_view render_node) = 0;

  virtual RunnerTypes serialize() const = 0;
};

struct App {
  moonlight::App base;

  std::string video_producer_buffer_caps;

  std::string h264_gst_pipeline;
  std::string hevc_gst_pipeline;
  std::string av1_gst_pipeline;

  std::string render_node;

  std::string opus_gst_pipeline;
  bool start_virtual_compositor;
  bool start_audio_server;
  std::shared_ptr<Runner> runner;
};

struct Profile {
  const std::string id;
  const std::string name;
  const std::string icon_png_path;
  /**
   * The pin that is required to access the profile
   * If this is not set, then the profile is open to everyone
   */
  std::optional<std::vector<short>> pin;

  std::shared_ptr<immer::atom<immer::vector<immer::box<App>>>> apps;
};

/**
 * There's going to be one special profile
 * which is going to be the one that holds the apps that will be shown in the Moonlight UI
 */
constexpr std::string_view MOONLIGHT_PROFILE_ID = "moonlight-profile-id";

struct Lobby {
  const std::string id;
  const std::string name;
  const std::string started_by_profile_id;
  std::optional<std::string> icon_png_path;
  const bool multi_user;
  /**
   * The pin that is required to join and control the lobby
   * If this is not set, then the lobby is open to everyone
   */
  std::optional<std::vector<short>> pin;

  const bool stop_when_everyone_leaves;
  /**
   * The app that is currently running in the lobby
   */
  std::shared_ptr<Runner> runner;

  /**
   * A list of all currently connected sessions by their session_id
   */
  std::shared_ptr<immer::atom<immer::vector<immer::box<std::string /* session_id */>>>> connected_sessions =
      std::make_shared<immer::atom<immer::vector<immer::box<std::string>>>>();

  /**
   * The wayland display that is currently being used by the lobby
   */
  std::shared_ptr<immer::atom<virtual_display::wl_state_ptr>> wayland_display =
      std::make_shared<immer::atom<virtual_display::wl_state_ptr>>();

  /**
   * The audio sink that is currently being used by the lobby
   */
  std::shared_ptr<immer::atom<std::shared_ptr<audio::VSink>>> audio_sink =
      std::make_shared<immer::atom<std::shared_ptr<audio::VSink>>>();

  /**
   * A queue of devices that will be plugged into the runner when ready
   */
  std::shared_ptr<events::devices_atom_queue> plugged_devices_queue = std::make_shared<events::devices_atom_queue>();
};

struct VideoSettings {
  int width;
  int height;
  int refresh_rate;
  std::string wayland_render_node;
  std::string runner_render_node;
  std::string video_producer_buffer_caps;
};

struct AudioSettings {
  int channel_count;
};

struct CreateLobbyEvent {
  const std::string id;
  std::string profile_id;
  const std::string name;
  const std::optional<std::string> icon_png_path;
  std::optional<std::vector<short>> pin = std::nullopt;
  const bool multi_user;
  const bool stop_when_everyone_leaves;

  const VideoSettings video_settings;

  const AudioSettings audio_settings;

  const config::ClientSettings client_settings = {};
  const std::string runner_state_folder;
  /**
   * The app that will be run in the lobby
   */
  std::shared_ptr<Runner> runner;

  /**
   * A promise to know when the lobby is up and running
   */
  rfl::Skip<std::shared_ptr<std::promise<bool>>> on_setup_over = std::make_shared<std::promise<bool>>();
};

struct JoinLobbyEvent {
  const std::string lobby_id;
  const std::size_t moonlight_session_id;
  std::optional<std::vector<short>> pin = std::nullopt;
  /**
   * A promise to know if there's been an error message
   */
  rfl::Skip<std::shared_ptr<std::promise<std::string>>> error_message = std::make_shared<std::promise<std::string>>();
};

struct LeaveLobbyEvent {
  const std::string lobby_id;
  const std::size_t moonlight_session_id;
};

struct StopLobbyEvent {
  const std::string lobby_id;
  std::optional<std::vector<short>> pin = std::nullopt;
};

struct DockerPullImageStartEvent {
  const std::string image_name;
};

struct DockerPullImageEndEvent {
  const std::string image_name;
  bool success;
};

struct DockerContainerCreated {
  std::string container_id;
  std::string hostname;
  std::string session_id;
};

struct DockerContainerStopped {
  std::string container_id;
  std::string hostname;
  std::string session_id;
};

using MouseTypes = std::variant<input::Mouse, virtual_display::WaylandMouse>;
using KeyboardTypes = std::variant<input::Keyboard, virtual_display::WaylandKeyboard>;
using TouchScreenTypes = std::variant<input::TouchScreen, virtual_display::WaylandTouchScreen>;
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
  std::string render_node;

  // A unique ID that identifies this session
  std::size_t session_id;

  std::uint16_t port;
  int timeout_ms;
  bool wait_for_ping = true;

  int packet_size;
  int frames_with_invalid_ref_threshold;
  int fec_percentage;
  int min_required_fec_packets;
  long bitrate_kbps;
  int slices_per_frame;

  ColorRange color_range;
  ColorSpace color_space;

  std::string client_ip;
  std::array<char, 16> rtp_secret_payload;
};

struct AudioSession {
  std::string gst_pipeline;

  // A unique ID that identifies this session
  std::size_t session_id;

  bool encrypt_audio;
  std::string aes_key;
  std::string aes_iv;

  std::uint16_t port;
  bool wait_for_ping = true;
  std::string client_ip;
  std::array<char, 16> rtp_secret_payload;

  int packet_duration;
  wolf::core::audio::AudioMode audio_mode;
};

struct IDRRequestEvent {
  // A unique ID that identifies this session
  std::size_t session_id;
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

struct ClientWolfUIComboEvent {
  std::size_t session_id;
};

struct SwitchStreamProducerEvents {
  std::size_t session_id;
  /**
   * The source ID of the interpipe that will be used to produce the stream.
   */
  std::string interpipe_src_id;
};

struct RTPVideoPingEvent {
  std::string client_ip;
  unsigned short client_port;
  rfl::Skip<std::shared_ptr<boost::asio::ip::udp::socket>> video_socket;
  std::optional<std::array<char, 16>> payload;
};

struct RTPAudioPingEvent {
  std::string client_ip;
  unsigned short client_port;
  rfl::Skip<std::shared_ptr<boost::asio::ip::udp::socket>> audio_socket;
  std::optional<std::array<char, 16>> payload;
};

struct StreamSession;

struct StartRunner {
  bool stop_stream_when_over = false;
  std::shared_ptr<Runner> runner;
  std::shared_ptr<StreamSession> stream_session;
};

using EventBusHandlers = dp::handler_registration<immer::box<PlugDeviceEvent>,
                                                  immer::box<PairSignal>,
                                                  immer::box<UnplugDeviceEvent>,
                                                  immer::box<StreamSession>,
                                                  immer::box<VideoSession>,
                                                  immer::box<AudioSession>,
                                                  immer::box<IDRRequestEvent>,
                                                  immer::box<PauseStreamEvent>,
                                                  immer::box<ResumeStreamEvent>,
                                                  immer::box<StopStreamEvent>,
                                                  immer::box<ClientWolfUIComboEvent>,
                                                  immer::box<RTPVideoPingEvent>,
                                                  immer::box<RTPAudioPingEvent>,
                                                  immer::box<StartRunner>,
                                                  immer::box<JoinLobbyEvent>,
                                                  immer::box<LeaveLobbyEvent>,
                                                  immer::box<CreateLobbyEvent>,
                                                  immer::box<StopLobbyEvent>,
                                                  immer::box<SwitchStreamProducerEvents>,
                                                  immer::box<DockerContainerCreated>,
                                                  immer::box<DockerContainerStopped>>;
using EventBusType = dp::event_bus<immer::box<PlugDeviceEvent>,
                                   immer::box<PairSignal>,
                                   immer::box<UnplugDeviceEvent>,
                                   immer::box<StreamSession>,
                                   immer::box<VideoSession>,
                                   immer::box<AudioSession>,
                                   immer::box<IDRRequestEvent>,
                                   immer::box<PauseStreamEvent>,
                                   immer::box<ResumeStreamEvent>,
                                   immer::box<StopStreamEvent>,
                                   immer::box<ClientWolfUIComboEvent>,
                                   immer::box<RTPVideoPingEvent>,
                                   immer::box<RTPAudioPingEvent>,
                                   immer::box<StartRunner>,
                                   immer::box<JoinLobbyEvent>,
                                   immer::box<LeaveLobbyEvent>,
                                   immer::box<CreateLobbyEvent>,
                                   immer::box<StopLobbyEvent>,
                                   immer::box<SwitchStreamProducerEvents>,
                                   immer::box<DockerContainerCreated>,
                                   immer::box<DockerContainerStopped>>;
using EventsVariant = std::variant<immer::box<PlugDeviceEvent>,
                                   immer::box<PairSignal>,
                                   immer::box<UnplugDeviceEvent>,
                                   immer::box<StreamSession>,
                                   immer::box<VideoSession>,
                                   immer::box<AudioSession>,
                                   immer::box<IDRRequestEvent>,
                                   immer::box<PauseStreamEvent>,
                                   immer::box<ResumeStreamEvent>,
                                   immer::box<StopStreamEvent>,
                                   immer::box<ClientWolfUIComboEvent>,
                                   immer::box<RTPVideoPingEvent>,
                                   immer::box<RTPAudioPingEvent>,
                                   immer::box<StartRunner>,
                                   immer::box<JoinLobbyEvent>,
                                   immer::box<LeaveLobbyEvent>,
                                   immer::box<CreateLobbyEvent>,
                                   immer::box<StopLobbyEvent>,
                                   immer::box<SwitchStreamProducerEvents>,
                                   immer::box<DockerContainerCreated>,
                                   immer::box<DockerContainerStopped>>;

/**
 * A StreamSession is created when a Moonlight user call `launch`
 *
 * This will then be fired up in the event_bus so that the rtsp, command, audio and video threads
 * can start working their magic.
 */
struct StreamSession {
  moonlight::DisplayMode display_mode;
  int audio_channel_count;

  std::shared_ptr<EventBusType> event_bus;
  immer::box<wolf::config::ClientSettings> client_settings;
  std::shared_ptr<App> app;
  std::string app_local_state_folder;
  std::string app_host_state_folder;

  // gcm encryption keys
  std::string aes_key;
  std::string aes_iv;

  // Moonlight protocol extension to support IP-less connections
  std::array<char, 16> rtp_secret_payload;
  uint32_t enet_secret_payload;
  /**
   * A dirty hack in order to support RTSP without IP.
   *
   * Moonlight will parrot back the IP that we send out in the launch or resume HTTPs requests.
   * So we send out a fake unique IP for each session in order to identify the session back in the RTSP thread.
   */
  std::string rtsp_fake_ip;

  // client info
  std::size_t session_id;
  std::string ip;

  unsigned short video_stream_port;
  unsigned short audio_stream_port;
  unsigned short control_stream_port;

  /**
   * Optional: the wayland display for the current session.
   * Will be only set during an active streaming and destroyed on stream end.
   */
  std::shared_ptr<immer::atom<virtual_display::wl_state_ptr>> wayland_display =
      std::make_shared<immer::atom<virtual_display::wl_state_ptr>>();

  std::shared_ptr<immer::atom<std::shared_ptr<audio::VSink>>> audio_sink =
      std::make_shared<immer::atom<std::shared_ptr<audio::VSink>>>();

  // virtual devices
  std::shared_ptr<std::optional<MouseTypes>> mouse = std::make_shared<std::optional<MouseTypes>>();
  std::shared_ptr<std::optional<KeyboardTypes>> keyboard = std::make_shared<std::optional<KeyboardTypes>>();
  std::shared_ptr<std::optional<TouchScreenTypes>> touch_screen =
      std::make_shared<std::optional<TouchScreenTypes>>(); /* Now added at the start */

  std::shared_ptr<immer::atom<JoypadList>> joypads = std::make_shared<immer::atom<JoypadList>>();

  std::shared_ptr<std::optional<input::PenTablet>> pen_tablet =
      std::make_shared<std::optional<input::PenTablet>>(); /* Optional, will be set on first use */
};

} // namespace wolf::core::events
