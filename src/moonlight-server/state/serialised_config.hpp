#pragma once

#include <helpers/utils.hpp>
#include <rfl.hpp>
#include <string>

namespace wolf::config {

enum class ControllerType {
  XBOX,
  PS,
  NINTENDO,
  AUTO
};

struct ClientSettings {
  /* The UID/GID that apps run as defaults to 1000:1000, but the defaults applied
   * to newly paired clients can be overridden via the WOLF_DEFAULT_RUN_UID /
   * WOLF_DEFAULT_RUN_GID environment variables. This lets deployments that don't
   * use the conventional 1000:1000 — for example Unraid, where `nobody` is
   * 99:100 — set sensible defaults without editing each client by hand. */
  uint run_uid = std::stoul(utils::get_env("WOLF_DEFAULT_RUN_UID", "1000"));
  uint run_gid = std::stoul(utils::get_env("WOLF_DEFAULT_RUN_GID", "1000"));
  /* A list of forced controller overrides, the position in the array denotes the controller number */
  std::vector<ControllerType> controllers_override = {};
  /* Values above 1.0 will make it faster, between 0.0 and 1.0 will make it slower */
  float mouse_acceleration = 1.0;
  /* (vertical scroll) Values above 1.0 will make it faster, between 0.0 and 1.0 will make it slower */
  float v_scroll_acceleration = 1.0;
  /* (horizontal scroll) Values above 1.0 will make it faster, between 0.0 and 1.0 will make it slower */
  float h_scroll_acceleration = 1.0;
  /* Motion-capable virtual pad override. When `controllers_override[slot]`
   * is unset, the client advertises GYRO/ACCELEROMETER, AND this is
   * anything other than `AUTO`, Wolf creates this controller type
   * instead of the auto-detected one. `AUTO` (default) defers to the
   * auto-detection logic (which promotes UNKNOWN-with-motion clients
   * to PS so motion routes — see `create_new_joypad` in
   * control/input_handler.cpp). Sibling to `controllers_override`:
   * same shape of "operator wants type X" but gated on the client
   * actually having motion to forward. */
  ControllerType motion_controller_override = ControllerType::AUTO;
};

struct PairedClient {
  std::string client_cert;
  std::string app_state_folder;
  ClientSettings settings = {};
};

struct GstEncoderDefault {
  std::string video_params;
  std::string video_params_zero_copy;
};

struct GstEncoder {
  std::string plugin_name;
  std::vector<std::string> check_elements;
  std::optional<std::string> video_params;
  std::optional<std::string> video_params_zero_copy;
  std::string encoder_pipeline;
};

struct GstVideoCfg {
  std::string default_source;
  std::string default_sink;
  std::map<std::string, GstEncoderDefault> defaults;

  std::vector<GstEncoder> av1_encoders;
  std::vector<GstEncoder> hevc_encoders;
  std::vector<GstEncoder> h264_encoders;
};

struct GstAudioCfg {
  std::string default_source;
  std::string default_audio_params;
  std::string default_opus_encoder;
  std::string default_sink;
};

struct GstreamerSettings {
  GstVideoCfg video;
  GstAudioCfg audio;
};

struct AppCMD {
  using Tag = rfl::Literal<"process", "Process">;
  std::string run_cmd;
};

struct AppDocker {
  using Tag = rfl::Literal<"docker", "Docker">;
  std::string name;
  std::string image;
  std::vector<std::string> mounts;
  std::vector<std::string> env;
  std::vector<std::string> devices;
  std::vector<std::string> ports;
  std::optional<std::string> base_create_json;
};

struct BaseAppVideoOverride {
  std::optional<std::string> source;
  std::optional<std::string> sink;
  std::optional<std::string> producer_buffer_caps;
  std::optional<std::string> video_params;
  std::optional<std::string> video_params_zero_copy;
  std::optional<std::string> h264_encoder;
  std::optional<std::string> hevc_encoder;
  std::optional<std::string> av1_encoder;
};

struct BaseAppAudioOverride {
  std::optional<std::string> source;
  std::optional<std::string> audio_params;
  std::optional<std::string> opus_encoder;
  std::optional<std::string> sink;
};

struct BaseApp {
  std::string title;
  std::optional<std::string> icon_png_path;
  std::optional<std::string> render_node;
  std::optional<BaseAppVideoOverride> video;
  std::optional<BaseAppAudioOverride> audio;
  std::optional<bool> start_virtual_compositor;
  std::optional<bool> start_audio_server;
  rfl::TaggedUnion<"type", AppCMD, AppDocker> runner =
      AppCMD{}; // We have to provide a default or rfl::DefaultIfMissing will fail
};

struct Profile {
  std::string id;
  std::optional<std::string> name;
  std::optional<std::string> icon_png_path;
  std::optional<std::vector<short>> pin;

  std::vector<BaseApp> apps;
};

struct WolfConfig {
  std::string hostname;
  std::string uuid;
  int config_version = 7;
  std::vector<PairedClient> paired_clients;
  std::vector<Profile> profiles;
  GstreamerSettings gstreamer;
};

struct BaseConfig {
  std::optional<int> config_version;
};

} // namespace wolf::config