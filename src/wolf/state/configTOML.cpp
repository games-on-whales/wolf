#include <fstream>
#include <range/v3/view.hpp>
#include <state/config.hpp>
#include <toml.hpp>

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
    this->default_source = toml::find_or<std::string>(v, "default_source", video::DEFAULT_SOURCE.data());
    this->default_video_params = toml::find_or<std::string>(v, "default_video_params", video::DEFAULT_PARAMS.data());
    this->default_h264_encoder =
        toml::find_or<std::string>(v, "default_h264_encoder", video::DEFAULT_H264_ENCODER.data());
    this->default_hevc_encoder =
        toml::find_or<std::string>(v, "default_hevc_encoder", video::DEFAULT_H265_ENCODER.data());
    this->default_sink = toml::find_or<std::string>(v, "default_sink", video::DEFAULT_SINK.data());
  }
};

struct GstAudioCfg {
  std::string default_source;
  std::string default_audio_params;
  std::string default_opus_encoder;
  std::string default_sink;

  void from_toml(const toml::value &v) {
    this->default_source = toml::find_or<std::string>(v, "default_source", audio::DEFAULT_SOURCE.data());
    this->default_audio_params = toml::find_or<std::string>(v, "default_audio_params", audio::DEFAULT_PARAMS.data());
    this->default_opus_encoder =
        toml::find_or<std::string>(v, "default_opus_encoder", audio::DEFAULT_OPUS_ENCODER.data());
    this->default_sink = toml::find_or<std::string>(v, "default_sink", audio::DEFAULT_SINK.data());
  }
};

void write(const toml::value &data, const std::string &dest) {
  std::ofstream out_file;
  out_file.open(dest);
  out_file << std::setw(80) << data;
  out_file.close();
}

Config get_default() {
  state::PairedClientList clients = {};
  auto atom = new immer::atom<state::PairedClientList>(clients);
  return Config{
      .uuid = gen_uuid(),
      .hostname = "wolf",
      .paired_clients = *atom,
      .apps = {{.base = {"Desktop", "1", true},
                .h264_gst_pipeline = video::DEFAULT_SOURCE.data() + " ! "s + video::DEFAULT_PARAMS.data() + " ! "s +
                                     video::DEFAULT_H264_ENCODER.data() + " ! " + video::DEFAULT_SINK.data(),
                .hevc_gst_pipeline = video::DEFAULT_SOURCE.data() + " ! "s + video::DEFAULT_PARAMS.data() + " ! "s +
                                     video::DEFAULT_H265_ENCODER.data() + " ! " + video::DEFAULT_SINK.data(),
                .opus_gst_pipeline = audio::DEFAULT_SOURCE.data() + " ! "s + audio::DEFAULT_PARAMS.data() + " ! "s +
                                     audio::DEFAULT_OPUS_ENCODER.data() + " ! " + audio::DEFAULT_SINK.data()}}};
}

Config load_or_default(const std::string &source) {
  if (file_exist(source)) {
    const auto cfg = toml::parse<toml::preserve_comments>(source);

    auto uuid = toml::find_or<std::string>(cfg, "uuid", gen_uuid());
    auto hostname = toml::find_or<std::string>(cfg, "hostname", "Wolf");

    GstVideoCfg default_gst_video_settings = toml::find<GstVideoCfg>(cfg, "gstreamer", "video");
    GstAudioCfg default_gst_audio_settings = toml::find<GstAudioCfg>(cfg, "gstreamer", "audio");

    auto cfg_clients = toml::find<std::vector<PairedClient>>(cfg, "paired_clients");
    auto paired_clients =
        cfg_clients                                                                                             //
        | ranges::views::transform([](const PairedClient &client) { return immer::box<PairedClient>{client}; }) //
        | ranges::to<immer::vector<immer::box<PairedClient>>>();

    auto cfg_apps = toml::find<std::vector<toml::value>>(cfg, "apps");
    auto apps =
        cfg_apps //
        | ranges::views::transform([&default_gst_audio_settings, &default_gst_video_settings](const toml::value &item) {
            auto h264_gst_pipeline =
                toml::find_or<std::string>(item, "video", "source", default_gst_video_settings.default_source) + " ! " +
                toml::find_or<std::string>(item,
                                           "video",
                                           "video_params",
                                           default_gst_video_settings.default_video_params) +
                " ! " +
                toml::find_or<std::string>(item,
                                           "video",
                                           "h264_encoder",
                                           default_gst_video_settings.default_h264_encoder) +
                " ! " + toml::find_or<std::string>(item, "video", "sink", default_gst_video_settings.default_sink);

            auto hevc_gst_pipeline =
                toml::find_or<std::string>(item, "video", "source", default_gst_video_settings.default_source) + " ! " +
                toml::find_or<std::string>(item,
                                           "video",
                                           "video_params",
                                           default_gst_video_settings.default_video_params) +
                " ! " +
                toml::find_or<std::string>(item,
                                           "video",
                                           "hevc_encoder",
                                           default_gst_video_settings.default_hevc_encoder) +
                " ! " + toml::find_or<std::string>(item, "video", "sink", default_gst_video_settings.default_sink);

            auto opus_gst_pipeline =
                toml::find_or<std::string>(item, "audio", "source", default_gst_audio_settings.default_source) + " ! " +
                toml::find_or<std::string>(item,
                                           "audio",
                                           "video_params",
                                           default_gst_audio_settings.default_audio_params) //
                + " ! " +                                                                   //
                toml::find_or<std::string>(item,
                                           "audio",
                                           "opus_encoder",
                                           default_gst_audio_settings.default_opus_encoder) //
                + " ! " +                                                                   //
                toml::find_or<std::string>(item, "audio", "sink", default_gst_audio_settings.default_sink);

            return state::App{.base = {.title = toml::find<std::string>(item, "title"),
                                       .id = toml::find<std::string>(item, "id"),
                                       .support_hdr = toml::find_or<bool>(item, "support_hdr", false)},
                              .h264_gst_pipeline = h264_gst_pipeline,
                              .hevc_gst_pipeline = hevc_gst_pipeline,
                              .opus_gst_pipeline = opus_gst_pipeline};
          })                                       //
        | ranges::to<immer::vector<state::App>>(); //

    auto clients_atom = new immer::atom<state::PairedClientList>(paired_clients);
    return Config{.uuid = uuid,
                  .hostname = hostname,
                  .config_source = source,
                  .paired_clients = *clients_atom,
                  .apps = apps};

  } else {
    logs::log(logs::warning, "Unable to open config file: {}, creating one using defaults", source);

    auto cfg = get_default();
    cfg.config_source = source;

    const toml::value data = {{"uuid", cfg.uuid},
                              {"hostname", cfg.hostname},
                              {"paired_clients", toml::array{}},
                              {"apps", cfg.apps},
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

    write(data, source); // write it back for future users

    return cfg;
  }
}

void pair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients.update(
      [&client](const state::PairedClientList &paired_clients) { return paired_clients.push_back(client); });

  // Update TOML
  toml::value tml = toml::parse<toml::preserve_comments>(cfg.config_source);

  tml.at("paired_clients")
      .as_array()
      .push_back({{"client_id", client.client_id},
                  {"client_cert", client.client_cert},
                  {"rtsp_port", client.rtsp_port},
                  {"control_port", client.control_port},
                  {"video_port", client.video_port},
                  {"audio_port", client.audio_port}});

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
    return state::PairedClient{
        .client_id = toml::find<std::string>(v, "client_id"),
        .client_cert = toml::find<std::string>(v, "client_cert"),
        .rtsp_port = toml::find_or<unsigned short>(v, "rtsp_port", state::RTSP_SETUP_PORT),
        .control_port = toml::find_or<unsigned short>(v, "control_port", state::CONTROL_PORT),
        .video_port = toml::find_or<unsigned short>(v, "video_port", state::VIDEO_STREAM_PORT),
        .audio_port = toml::find_or<unsigned short>(v, "audio_port", state::AUDIO_STREAM_PORT),
    };
  }
};

template <> struct into<state::PairedClient> {
  static toml::value into_toml(const state::PairedClient &f) {
    return toml::value{{"client_id", f.client_id},
                       {"client_cert", f.client_cert},
                       {"rtsp_port", f.rtsp_port},
                       {"control_port", f.control_port},
                       {"video_port", f.video_port},
                       {"audio_port", f.audio_port}};
  }
};

template <> struct into<state::App> {

  static toml::value into_toml(const state::App &f) {
    return toml::value{
        {"title", f.base.title},
        {"id", f.base.id},
        {"support_hdr", f.base.support_hdr},
        // TODO: [video] [audio] are they needed?
    };
  }
};
} // namespace toml