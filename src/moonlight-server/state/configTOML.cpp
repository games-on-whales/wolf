#include <fstream>
#include <range/v3/view.hpp>
#include <runners/docker.hpp>
#include <runners/process.hpp>
#include <state/config.hpp>
#include <toml.hpp>
#include <utility>
#include <gst/gstregistry.h>
#include <gst/gstelementfactory.h>

namespace state {

struct GstEncoder {
  std::string plugin_name;
  std::vector<std::string> check_elements;
  std::string video_params;
  std::string encoder_pipeline;
};

struct GstVideoCfg {
  std::string default_source;
  std::string default_sink;
  std::vector<GstEncoder> hevc_encoders;
  std::vector<GstEncoder> h264_encoders;
};

struct GstAudioCfg {
  std::string default_source;
  std::string default_audio_params;
  std::string default_opus_encoder;
  std::string default_sink;
};
} // namespace state

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(state::GstEncoder, plugin_name, check_elements, video_params, encoder_pipeline)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(state::GstVideoCfg, default_source, default_sink, hevc_encoders, h264_encoders)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(
    state::GstAudioCfg, default_source, default_audio_params, default_opus_encoder, default_sink)

namespace toml {

template <> struct into<state::App> {

  static toml::value into_toml(const state::App &f) {
    return toml::value{{"title", f.base.title}, {"support_hdr", f.base.support_hdr}, {"runner", f.runner->serialise()}};
  }
};

template <> struct into<state::PairedClient> {
  static toml::value into_toml(const state::PairedClient &c) {
    return toml::value{{"client_cert", c.client_cert},
                       {"app_state_folder", c.app_state_folder},
                       {"run_uid", c.run_uid},
                       {"run_gid", c.run_gid}};
  }
};

template <> struct from<state::PairedClient> {
  template <typename C, template <typename...> class M, template <typename...> class A>
  static state::PairedClient from_toml(const basic_value<C, M, A> &v) {
    state::PairedClient client;

    client.client_cert = find<std::string>(v, "client_cert");
    client.app_state_folder =
        find_or<std::string>(v, "app_state_folder", std::to_string(std::hash<std::string>{}(client.client_cert)));
    client.run_uid = find_or<uint>(v, "run_uid", 1000);
    client.run_gid = find_or<uint>(v, "run_gid", 1000);

    return client;
  }
};
} // namespace toml

namespace state {

/**
 * A bit of magic here, it'll load up the default/config.toml via Cmake (look for `make_includable`)
 */
constexpr char const *default_toml =
#include "default/config.include.toml"
    ;

using namespace std::literals;

void write(const toml::value &data, const std::string &dest) {
  std::ofstream out_file;
  out_file.open(dest);
  out_file << toml::format(data, 120);
  out_file.close();
}

void create_default(const std::string &source) {
  std::ofstream out_file;
  out_file.open(source);
  out_file << "# A unique identifier for this host" << std::endl;
  out_file << "uuid = \"" << gen_uuid() << "\"" << std::endl;
  out_file << default_toml;
  out_file.close();
}

std::shared_ptr<state::Runner> get_runner(const toml::value &item, const std::shared_ptr<dp::event_bus> &ev_bus) {
  auto runner_obj = toml::find_or(item, "runner", {{"type", "process"}});
  auto runner_type = toml::find_or(runner_obj, "type", "process");
  if (runner_type == "process") {
    auto run_cmd = toml::find_or(runner_obj, "run_cmd", "sh -c \"while :; do echo 'running...'; sleep 1; done\"");
    return std::make_shared<process::RunProcess>(ev_bus, run_cmd);
  } else if (runner_type == "docker") {
    return std::make_shared<docker::RunDocker>(docker::RunDocker::from_toml(ev_bus, runner_obj));
  } else {
    logs::log(logs::warning, "[TOML] Found runner of type: {}, valid types are: 'process' or 'docker'", runner_type);
    return std::make_shared<process::RunProcess>(ev_bus, "sh -c \"while :; do echo 'running...'; sleep 1; done\"");
  }
}

static bool is_available(const GstEncoder &settings) {
  if (auto plugin = gst_registry_find_plugin(gst_registry_get(), settings.plugin_name.c_str())) {
    gst_object_unref(plugin);
    return std::all_of(settings.check_elements.begin(), settings.check_elements.end(), [](const auto &el_name) {
      if (auto el = gst_element_factory_make(el_name.c_str(), nullptr)) {
        gst_object_unref(el);
        return true;
      } else {
        return false;
      }
    });
  }
  return false;
}

static state::Encoder encoder_type(const std::string &gstreamer_plugin_name) {
  switch (utils::hash(gstreamer_plugin_name)) {
  case (utils::hash("nvcodec")):
    return NVIDIA;
  case (utils::hash("vaapi")):
  case (utils::hash("va")):
    return VAAPI;
  case (utils::hash("qsv")):
    return QUICKSYNC;
  case (utils::hash("applemedia")):
    return APPLE;
  case (utils::hash("x264")):
  case (utils::hash("x265")):
    return SOFTWARE;
  }
  logs::log(logs::warning, "Unrecognised Gstreamer plugin name: {}", gstreamer_plugin_name);
  return UNKNOWN;
}

toml::value v1_to_v2(const toml::value &v1, const std::string &source) {
  create_default(source);
  auto v2 = toml::parse<toml::preserve_comments>(source);
  v2["hostname"] = v1.at("hostname").as_string();
  v2["uuid"] = v1.at("uuid").as_string();
  v2["support_hevc"] = v1.at("support_hevc").as_boolean();
  v2["paired_clients"] = v1.at("paired_clients").as_array() |                                        //
                         ranges::views::transform([](const toml::value &client) {                    //
                           return PairedClient{.client_cert = client.at("client_cert").as_string()}; //
                         })                                                                          //
                         | ranges::to<toml::array>();
  write(v2, source);
  return v2;
}

Config load_or_default(const std::string &source, const std::shared_ptr<dp::event_bus> &ev_bus) {
  if (!file_exist(source)) {
    logs::log(logs::warning, "Unable to open config file: {}, creating one using defaults", source);
    create_default(source);
  }

  auto cfg = toml::parse<toml::preserve_comments>(source);
  auto version = toml::find_or(cfg, "config_version", 1);
  if (version <= 1) {
    logs::log(logs::warning, "Found old config file, migrating to newer version");
    cfg = v1_to_v2(cfg, source);
  }

  std::string uuid;
  if (cfg.contains("uuid")) {
    uuid = toml::find(cfg, "uuid").as_string();
  } else {
    logs::log(logs::warning, "No uuid found, generating a new one");
    uuid = gen_uuid();
    cfg["uuid"] = uuid;
    write(cfg, source);
  }

  auto hostname = toml::find_or(cfg, "hostname", "Wolf");

  GstVideoCfg default_gst_video_settings = toml::find<GstVideoCfg>(cfg, "gstreamer", "video");
  GstAudioCfg default_gst_audio_settings = toml::find<GstAudioCfg>(cfg, "gstreamer", "audio");

  /* Automatic pick best H264 encoder */
  auto h264_encoder = std::find_if(default_gst_video_settings.h264_encoders.begin(),
                                   default_gst_video_settings.h264_encoders.end(),
                                   is_available);
  if (h264_encoder == std::end(default_gst_video_settings.h264_encoders)) {
    throw std::runtime_error("Unable to find a compatible H264 encoder, please check [[gstreamer.video.h264_encoders]] "
                             "in your config.toml or your Gstreamer installation");
  }
  logs::log(logs::info, "Selected H264 encoder: {}", h264_encoder->plugin_name);

  /* Automatic pick best HEVC encoder */
  auto hevc_encoder = std::find_if(default_gst_video_settings.hevc_encoders.begin(),
                                   default_gst_video_settings.hevc_encoders.end(),
                                   is_available);
  if (hevc_encoder == std::end(default_gst_video_settings.hevc_encoders)) {
    throw std::runtime_error("Unable to find a compatible HEVC encoder, please check [[gstreamer.video.hevc_encoders]] "
                             "in your config.toml or your Gstreamer installation");
  }
  logs::log(logs::info, "Selected HEVC encoder: {}", hevc_encoder->plugin_name);

  /* Get paired clients */
  auto cfg_clients = toml::find<std::vector<PairedClient>>(cfg, "paired_clients");
  auto paired_clients =
      cfg_clients                                                                                             //
      | ranges::views::transform([](const PairedClient &client) { return immer::box<PairedClient>{client}; }) //
      | ranges::to<immer::vector<immer::box<PairedClient>>>();

  std::string default_app_render_node = utils::get_env("WOLF_RENDER_NODE", "/dev/dri/renderD128");
  /* Get apps, here we'll merge the default gstreamer settings with the app specific overrides */
  auto cfg_apps = toml::find<std::vector<toml::value>>(cfg, "apps");
  auto apps =
      cfg_apps |                                                               //
      ranges::views::enumerate |                                               //
      ranges::views::transform([&](std::pair<int, const toml::value &> pair) { //
        auto [idx, item] = pair;
        auto h264_gst_pipeline = toml::find_or(item, "video", "source", default_gst_video_settings.default_source) +
                                 " ! " + toml::find_or(item, "video", "video_params", h264_encoder->video_params) +
                                 " ! " + toml::find_or(item, "video", "h264_encoder", h264_encoder->encoder_pipeline) +
                                 " ! " +
                                 toml::find_or(item, "video", " sink ", default_gst_video_settings.default_sink);

        auto hevc_gst_pipeline = toml::find_or(item, "video", "source", default_gst_video_settings.default_source) +
                                 " ! " + toml::find_or(item, "video", "video_params", hevc_encoder->video_params) +
                                 " ! " + toml::find_or(item, "video", "hevc_encoder", hevc_encoder->encoder_pipeline) +
                                 " ! " +
                                 toml::find_or(item, "video", " sink ", default_gst_video_settings.default_sink);

        auto opus_gst_pipeline =
            toml::find_or(item, "audio", "source", default_gst_audio_settings.default_source) + " ! " +
            toml::find_or(item, "audio", "video_params", default_gst_audio_settings.default_audio_params) + " ! " +
            toml::find_or(item, "audio", "opus_encoder", default_gst_audio_settings.default_opus_encoder) + " ! " +
            toml::find_or(item, "audio", "sink", default_gst_audio_settings.default_sink);

        return state::App{.base = {.title = toml::find<std::string>(item, "title"),
                                   .id = std::to_string(idx + 1),
                                   .support_hdr = toml::find_or<bool>(item, "support_hdr", false)},
                          .h264_gst_pipeline = h264_gst_pipeline,
                          .h264_encoder = encoder_type(h264_encoder->plugin_name),
                          .hevc_gst_pipeline = hevc_gst_pipeline,
                          .hevc_encoder = encoder_type(hevc_encoder->plugin_name),
                          .render_node = toml::find_or(item, "render_node", default_app_render_node),

                          .opus_gst_pipeline = opus_gst_pipeline,
                          .start_virtual_compositor = toml::find_or<bool>(item, "start_virtual_compositor", true),
                          .runner = get_runner(item, ev_bus)};
      }) |                                     //
      ranges::to<immer::vector<state::App>>(); //

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
  tml.at("paired_clients").as_array().emplace_back(client);

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