#include <fstream>
#include <range/v3/view.hpp>
#include <runners/process.hpp>
#include <state/config.hpp>
#include <toml.hpp>
#include <utility>

namespace state {

using namespace gstreamer;
using namespace std::literals;

struct GstVideoCfg {
  std::string default_source;
  std::string default_video_params;
  std::string default_h264_encoder;
  std::string default_hevc_encoder;
  std::string default_sink;

  void from_toml(const toml::value &v) {
    this->default_source = toml::find_or(v, "default_source", video::DEFAULT_SOURCE.data());
    this->default_video_params = toml::find_or(v, "default_video_params", video::DEFAULT_PARAMS.data());
    this->default_h264_encoder = toml::find_or(v, "default_h264_encoder", video::DEFAULT_H264_ENCODER.data());
    this->default_hevc_encoder = toml::find_or(v, "default_hevc_encoder", video::DEFAULT_H265_ENCODER.data());
    this->default_sink = toml::find_or(v, "default_sink", video::DEFAULT_SINK.data());
  }
};

struct GstAudioCfg {
  std::string default_source;
  std::string default_audio_params;
  std::string default_opus_encoder;
  std::string default_sink;

  void from_toml(const toml::value &v) {
    this->default_source = toml::find_or(v, "default_source", audio::DEFAULT_SOURCE.data());
    this->default_audio_params = toml::find_or(v, "default_audio_params", audio::DEFAULT_PARAMS.data());
    this->default_opus_encoder = toml::find_or(v, "default_opus_encoder", audio::DEFAULT_OPUS_ENCODER.data());
    this->default_sink = toml::find_or(v, "default_sink", audio::DEFAULT_SINK.data());
  }
};

void write(const toml::value &data, const std::string &dest) {
  std::ofstream out_file;
  out_file.open(dest);
  out_file << std::setw(80) << data;
  out_file.close();
}

std::shared_ptr<state::Runner> get_runner(const toml::value &item, const std::shared_ptr<dp::event_bus> &ev_bus) {
  auto runner_obj = toml::find_or(item, "runner", {{"type", "RunProcess"}});
  auto runner_type = toml::find_or(runner_obj, "type", "RunProcess");
  if (runner_type == "RunProcess") {
    auto run_cmd = toml::find_or(runner_obj, "run_cmd", "sh -c \"while :; do echo 'running...'; sleep 1; done\"");
    return std::make_shared<process::RunProcess>(ev_bus, run_cmd);
  } else {
    logs::log(logs::warning, "[TOML] Found runner of type: {}, valid types are: 'RunProcess' or 'Docker'", runner_type);
    return std::make_shared<process::RunProcess>(ev_bus, "sh -c \"while :; do echo 'running...'; sleep 1; done\"");
  }
}

Config load_or_default(const std::string &source, const std::shared_ptr<dp::event_bus> &ev_bus) {
  if (!file_exist(source)) {
    {
      logs::log(logs::warning, "Unable to open config file: {}, creating one using defaults", source);

      auto video_test = video::DEFAULT_SOURCE;
      auto x11_src = "ximagesrc show-pointer=true use-damage=false ! video/x-raw, framerate={fps}/1";
      auto wayland_src = "waylanddisplaysrc ! video/x-raw, framerate={fps}/1";
      auto pulse_src = "pulsesrc";

      auto default_app = toml::value{{"title", "Test ball (auto)"}, {"video", {{"source", video_test}}}};
      auto x11_auto =
          toml::value{{"title", "X11 (auto)"}, {"video", {{"source", x11_src}}}, {"audio", {{"source", pulse_src}}}};
      auto wayland_auto = toml::value{{"title", "Wayland Display (auto)"},
                                      {"video", {{"source", wayland_src}}},
                                      {"audio", {{"source", pulse_src}}}};

      /* VAAPI specific encoder */

      auto h264_vaapi = "vaapih264enc max-bframes=0 refs=1 num-slices={slices_per_frame} bitrate={bitrate} ! "
                        "h264parse ! "
                        "video/x-h264, profile=main, stream-format=byte-stream";
      auto hevc_vaapi = "vaapih265enc max-bframes=0 refs=1 num-slices={slices_per_frame} bitrate={bitrate} ! "
                        "h265parse ! "
                        "video/x-h265, profile=main, stream-format=byte-stream";
      auto video_vaapi = "vaapipostproc ! "
                         "video/x-raw(memory:VASurface), chroma-site={color_range}, width={width}, "
                         "height={height}, format=NV12, colorimetry={color_space}";

      auto test_vaapi = toml::value{{"title", "Test ball (VAAPI)"},
                                    {"video",
                                     {{"source", video_test},
                                      {"h264_encoder", h264_vaapi},
                                      {"hevc_encoder", hevc_vaapi},
                                      {"video_params", video_vaapi}}}};
      auto x11_vaapi = toml::value{{"title", "X11 (VAAPI)"},
                                   {"video",
                                    {{"source", x11_src},
                                     {"h264_encoder", h264_vaapi},
                                     {"hevc_encoder", hevc_vaapi},
                                     {"video_params", video_vaapi}}},
                                   {"audio", {{"source", pulse_src}}}};
      auto wayland_vaapi = toml::value{{"title", "Wayland Display (VAAPI)"},
                                       {"video",
                                        {{"source", wayland_src},
                                         {"h264_encoder", h264_vaapi},
                                         {"hevc_encoder", hevc_vaapi},
                                         {"video_params", video_vaapi}}},
                                       {"audio", {{"source", pulse_src}}}};

      /* CUDA specific encoder */
      // TODO: gop-size here should be -1 but it's not playing with Moonlight
      auto h264_cuda = "nvh264enc preset=low-latency-hq zerolatency=true gop-size=0 bitrate={bitrate} aud=false ! "
                       "h264parse ! "
                       "video/x-h264, profile=main, stream-format=byte-stream";
      auto hevc_cuda = "nvh265enc preset=low-latency-hq zerolatency=true bitrate={bitrate} aud=false ! "
                       "h265parse ! "
                       "video/x-h265, profile=main, stream-format=byte-stream";
      auto video_cuda = " queue !"
                        " cudaupload !"
                        " cudascale ! "
                        " cudaconvert ! "
                        " video/x-raw(memory:CUDAMemory), width={width}, height={height}, "
                        " chroma-site={color_range}, format=NV12, colorimetry={color_space}";

      auto test_cuda = toml::value{{"title", "Test ball (CUDA)"},
                                   {"video",
                                    {{"source", video_test},
                                     {"h264_encoder", h264_cuda},
                                     {"hevc_encoder", hevc_cuda},
                                     {"video_params", video_cuda}}}};
      auto x11_cuda = toml::value{{"title", "X11 (CUDA)"},
                                  {"video",
                                   {{"source", x11_src},
                                    {"h264_encoder", h264_cuda},
                                    {"hevc_encoder", hevc_cuda},
                                    {"video_params", video_cuda}}},
                                  {"audio", {{"source", pulse_src}}}};
      auto wayland_cuda = toml::value{{"title", "Wayland Display (CUDA)"},
                                      {"video",
                                       {{"source", wayland_src},
                                        {"h264_encoder", h264_cuda},
                                        {"hevc_encoder", hevc_cuda},
                                        {"video_params", video_cuda}}},
                                      {"audio", {{"source", pulse_src}}}};

      const toml::value data = {{"uuid", gen_uuid()},
                                {"hostname", "Wolf"},
                                {"support_hevc", true},
                                {"paired_clients", toml::array{}},
                                {"apps",
                                 {default_app,
                                  x11_auto,
                                  wayland_auto,
                                  test_vaapi,
                                  x11_vaapi,
                                  wayland_vaapi,
                                  test_cuda,
                                  x11_cuda,
                                  wayland_cuda}},
                                {"gstreamer", // key
                                 {            // array
                                  {
                                      "video",
                                      {{"default_source", video::DEFAULT_SOURCE},
                                       {"default_video_params", video::DEFAULT_PARAMS},
                                       {"default_h264_encoder", video::DEFAULT_H264_ENCODER},
                                       {"default_hevc_encoder", video::DEFAULT_H265_ENCODER},
                                       {"default_sink", video::DEFAULT_SINK}},
                                  },
                                  {
                                      "audio",
                                      {{"default_source", audio::DEFAULT_SOURCE},
                                       {"default_audio_params", audio::DEFAULT_PARAMS},
                                       {"default_opus_encoder", audio::DEFAULT_OPUS_ENCODER},
                                       {"default_sink", audio::DEFAULT_SINK}},
                                  }}}};

      write(data, source); // write it back
    }
  }
  const auto cfg = toml::parse<toml::preserve_comments>(source);

  auto uuid = toml::find_or(cfg, "uuid", gen_uuid());
  auto hostname = toml::find_or(cfg, "hostname", "Wolf");

  GstVideoCfg default_gst_video_settings = toml::find<GstVideoCfg>(cfg, "gstreamer", "video");
  GstAudioCfg default_gst_audio_settings = toml::find<GstAudioCfg>(cfg, "gstreamer", "audio");

  auto cfg_clients = toml::find<std::vector<PairedClient>>(cfg, "paired_clients");
  auto paired_clients =
      cfg_clients                                                                                             //
      | ranges::views::transform([](const PairedClient &client) { return immer::box<PairedClient>{client}; }) //
      | ranges::to<immer::vector<immer::box<PairedClient>>>();

  auto cfg_apps = toml::find<std::vector<toml::value>>(cfg, "apps");
  auto apps =
      cfg_apps                   //
      | ranges::views::enumerate //
      | ranges::views::transform([&](std::pair<int, const toml::value &> pair) {
          auto [idx, item] = pair;
          auto h264_gst_pipeline =
              toml::find_or(item, "video", "source", default_gst_video_settings.default_source) + " ! " +
              toml::find_or(item, "video", "video_params", default_gst_video_settings.default_video_params) + " ! " +
              toml::find_or(item, "video", "h264_encoder", default_gst_video_settings.default_h264_encoder) + " ! " +
              toml::find_or(item, "video", "sink", default_gst_video_settings.default_sink);

          auto hevc_gst_pipeline =
              toml::find_or(item, "video", "source", default_gst_video_settings.default_source) + " ! " +
              toml::find_or(item, "video", "video_params", default_gst_video_settings.default_video_params) + " ! " +
              toml::find_or(item, "video", "hevc_encoder", default_gst_video_settings.default_hevc_encoder) + " ! " +
              toml::find_or(item, "video", "sink", default_gst_video_settings.default_sink);

          auto opus_gst_pipeline = toml::find_or(item, "audio", "source", default_gst_audio_settings.default_source) +
                                   " ! " +
                                   toml::find_or(item,
                                                 "audio",
                                                 "video_params",
                                                 default_gst_audio_settings.default_audio_params) //
                                   + " ! " +                                                      //
                                   toml::find_or(item,
                                                 "audio",
                                                 "opus_encoder",
                                                 default_gst_audio_settings.default_opus_encoder) //
                                   + " ! " +                                                      //
                                   toml::find_or(item, "audio", "sink", default_gst_audio_settings.default_sink);

          return state::App{.base = {.title = toml::find<std::string>(item, "title"),
                                     .id = std::to_string(idx + 1), // Moonlight expects: 1,2,3 ...
                                     .support_hdr = toml::find_or<bool>(item, "support_hdr", false)},
                            .h264_gst_pipeline = h264_gst_pipeline,
                            .hevc_gst_pipeline = hevc_gst_pipeline,
                            .opus_gst_pipeline = opus_gst_pipeline,
                            .runner = get_runner(item, ev_bus)};
        })                                       //
      | ranges::to<immer::vector<state::App>>(); //

  auto clients_atom = new immer::atom<state::PairedClientList>(paired_clients);
  return Config{.uuid = uuid,
                .hostname = hostname,
                .config_source = source,
                .support_hevc = toml::find_or<bool>(cfg, "support_hevc", false),
                .paired_clients = *clients_atom,
                .apps = apps};
}

void pair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients.update(
      [&client](const state::PairedClientList &paired_clients) { return paired_clients.push_back(client); });

  // Update TOML
  toml::value tml = toml::parse<toml::preserve_comments>(cfg.config_source);

  tml.at("paired_clients").as_array().push_back({{"client_id", client.client_id}, {"client_cert", client.client_cert}});

  write(tml, cfg.config_source);
}

void unpair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients.update([&client](const state::PairedClientList &paired_clients) {
    return paired_clients                                               //
           | ranges::views::filter([&client](auto paired_client) {      //
               return paired_client->client_cert != client.client_cert; //
             })                                                         //
           | ranges::to<state::PairedClientList>();                     //
  });

  // Update TOML
  toml::value tml = toml::parse<toml::preserve_comments>(cfg.config_source);

  auto &saved_clients = tml.at("paired_clients").as_array();
  saved_clients.erase(std::remove_if(saved_clients.begin(),
                                     saved_clients.end(),
                                     [&client](const toml::value &v) {
                                       auto cert = toml::find<std::string>(v, "client_cert");
                                       return cert == client.client_cert;
                                     }),
                      saved_clients.end());

  write(tml, cfg.config_source);
}

} // namespace state

namespace toml {
template <> struct from<state::PairedClient> {

  static state::PairedClient from_toml(const value &v) {
    return state::PairedClient{.client_id = toml::find<std::string>(v, "client_id"),
                               .client_cert = toml::find<std::string>(v, "client_cert")};
  }
};

template <> struct into<state::PairedClient> {
  static toml::value into_toml(const state::PairedClient &f) {
    return toml::value{{"client_id", f.client_id}, {"client_cert", f.client_cert}};
  }
};

template <> struct into<state::App> {

  static toml::value into_toml(const state::App &f) {
    return toml::value{{"title", f.base.title}, {"support_hdr", f.base.support_hdr}, {"runner", f.runner->serialise()}};
  }
};
} // namespace toml