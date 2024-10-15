#pragma once

#include <rfl.hpp>

namespace wolf::config {

enum class ControllerType {
  XBOX,
  PS,
  NINTENDO,
  AUTO
};

struct ClientSettings {
  uint run_uid = 1000;
  uint run_gid = 1000;
  /* A list of forced controller overrides, the position in the array denotes the controller number */
  std::vector<ControllerType> controllers_override = {};
  /* Values above 1.0 will make it faster, between 0.0 and 1.0 will make it slower */
  float mouse_acceleration = 1.0;
  /* (vertical scroll) Values above 1.0 will make it faster, between 0.0 and 1.0 will make it slower */
  float v_scroll_acceleration = 1.0;
  /* (horizontal scroll) Values above 1.0 will make it faster, between 0.0 and 1.0 will make it slower */
  float h_scroll_acceleration = 1.0;
};

struct PairedClient {
  std::string client_cert;
  std::string app_state_folder;
  ClientSettings settings = {};
};

struct GstEncoderDefault {
  std::string video_params;
};

struct GstEncoder {
  std::string plugin_name;
  std::vector<std::string> check_elements;
  std::optional<std::string> video_params;
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

struct AppChildSession {
  using Tag = rfl::Literal<"child_session">;
  std::string parent_session_id;
};

struct BaseAppVideoOverride {
  std::optional<std::string> source;
  std::optional<std::string> sink;
  std::optional<std::string> video_params;
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
  rfl::TaggedUnion<"type", AppCMD, AppDocker, AppChildSession> runner =
      AppCMD{}; // We have to provide a default or rfl::DefaultIfMissing will fail
};

struct WolfConfig {
  std::string hostname;
  std::string uuid;
  int config_version = 4;
  std::vector<PairedClient> paired_clients;
  std::vector<BaseApp> apps;
  GstreamerSettings gstreamer;
};

struct BaseConfig {
  std::optional<int> config_version;
};

} // namespace wolf::config