#include <fstream>
#include <gst/gstelementfactory.h>
#include <gst/gstregistry.h>
#include <platforms/hw.hpp>
#include <range/v3/view.hpp>
#include <runners/docker.hpp>
#include <runners/process.hpp>
#include <state/config.hpp>
#include <toml.hpp>
#include <utility>

namespace state {

struct GstEncoderDefault {
  std::string video_params = "";
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
} // namespace state

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(state::GstEncoderDefault, video_params)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(state::GstEncoder, plugin_name, check_elements, video_params, encoder_pipeline)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(
    state::GstVideoCfg, default_source, default_sink, defaults, av1_encoders, hevc_encoders, h264_encoders)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(
    state::GstAudioCfg, default_source, default_audio_params, default_opus_encoder, default_sink)

namespace toml {

template <> struct into<events::App> {

  static toml::value into_toml(const events::App &f) {
    return toml::table{{"title", f.base.title}, {"support_hdr", f.base.support_hdr}, {"runner", f.runner->serialise()}};
  }
};

template <> struct into<state::PairedClient> {
  template <typename TC> static toml::basic_value<TC> into_toml(const state::PairedClient &c) {
    return toml::table{{"client_cert", c.client_cert},
                       {"app_state_folder", c.app_state_folder},
                       {"run_uid", c.run_uid},
                       {"run_gid", c.run_gid}};
  }
};

template <> struct from<state::PairedClient> {
  static state::PairedClient from_toml(const toml::value &v) {
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
  out_file << toml::format(data);
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

std::shared_ptr<events::Runner> get_runner(const toml::value &item,
                                           const std::shared_ptr<dp::event_bus<events::EventTypes>> &ev_bus) {
  auto runner_obj = toml::find_or(item, "runner", toml::value{toml::table{{"type", "process"}}});
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

static state::Encoder encoder_type(const GstEncoder &settings) {
  switch (utils::hash(settings.plugin_name)) {
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
  case (utils::hash("aom")):
    return SOFTWARE;
  }
  logs::log(logs::warning, "Unrecognised Gstreamer plugin name: {}", settings.plugin_name);
  return UNKNOWN;
}

static bool is_available(const GPU_VENDOR &gpu_vendor, const GstEncoder &settings) {
  if (auto plugin = gst_registry_find_plugin(gst_registry_get(), settings.plugin_name.c_str())) {
    gst_object_unref(plugin);
    return std::all_of(
        settings.check_elements.begin(),
        settings.check_elements.end(),
        [settings, gpu_vendor](const auto &el_name) {
          // Is the selected GPU vendor compatible with the encoder?
          // (Particularly useful when using multiple GPUs, e.g. nvcodec might be available but user
          // wants to encode using the Intel GPU)
          auto encoder_vendor = encoder_type(settings);
          if (encoder_vendor == NVIDIA && gpu_vendor != GPU_VENDOR::NVIDIA) {
            logs::log(logs::debug, "Skipping NVIDIA encoder, not a NVIDIA GPU ({})", (int)gpu_vendor);
          } else if (encoder_vendor == VAAPI && (gpu_vendor != GPU_VENDOR::INTEL && gpu_vendor != GPU_VENDOR::AMD)) {
            logs::log(logs::debug, "Skipping VAAPI encoder, not an Intel or AMD GPU ({})", (int)gpu_vendor);
          } else if (encoder_vendor == QUICKSYNC && gpu_vendor != GPU_VENDOR::INTEL) {
            logs::log(logs::debug, "Skipping QUICKSYNC encoder, not an Intel GPU ({})", (int)gpu_vendor);
          }
          // Can Gstreamer instantiate the element? This will only work if all the drivers are in place
          else if (auto el = gst_element_factory_make(el_name.c_str(), nullptr)) {
            gst_object_unref(el);
            return true;
          }

          return false;
        });
  }
  return false;
}

std::optional<GstEncoder>
get_encoder(std::string_view tech, const std::vector<GstEncoder> &encoders, const GPU_VENDOR &vendor) {
  auto default_is_available = std::bind(is_available, vendor, std::placeholders::_1);
  auto encoder = std::find_if(encoders.begin(), encoders.end(), default_is_available);
  if (encoder != std::end(encoders)) {
    logs::log(logs::info, "Using {} encoder: {}", tech, encoder->plugin_name);
    if (encoder_type(*encoder) == SOFTWARE) {
      logs::log(logs::warning, "Software {} encoder detected", tech);
    }
    return *encoder;
  }
  return std::nullopt;
}

toml::value v3_to_v4(const toml::value &v3, const std::string &source) {
  std::filesystem::rename(source, source + ".v3.old");
  create_default(source);
  auto v4 = toml::parse(source);
  // Copy back everything apart from the Gstreamer pipelines
  v4["hostname"] = v3.at("hostname").as_string();
  v4["uuid"] = v3.at("uuid").as_string();
  v4["apps"] = v3.at("apps");
  v4["paired_clients"] = v3.at("paired_clients");
  write(v4, source);
  return v4;
}

Config load_or_default(const std::string &source, const std::shared_ptr<dp::event_bus<events::EventTypes>> &ev_bus) {
  if (!file_exist(source)) {
    logs::log(logs::warning, "Unable to open config file: {}, creating one using defaults", source);
    create_default(source);
  }

  auto cfg = toml::parse(source);
  auto version = toml::find_or(cfg, "config_version", 2);
  if (version <= 3) {
    logs::log(logs::warning, "Found old config file, migrating to newer version");
    cfg = v3_to_v4(cfg, source);
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

  auto default_gst_video_settings = toml::find<GstVideoCfg>(cfg, "gstreamer", "video");
  auto default_gst_audio_settings = toml::find<GstAudioCfg>(cfg, "gstreamer", "audio");
  auto default_gst_encoder_settings = default_gst_video_settings.defaults;

  auto default_app_render_node = utils::get_env("WOLF_RENDER_NODE", "/dev/dri/renderD128");
  auto default_gst_render_node = utils::get_env("WOLF_ENCODER_NODE", default_app_render_node);
  auto vendor = get_vendor(default_gst_render_node);

  /* Automatic pick best encoders */
  auto h264_encoder = get_encoder("H264", default_gst_video_settings.h264_encoders, vendor);
  if (!h264_encoder) {
    throw std::runtime_error(
        "Unable to find a compatible H.264 encoder, please check [[gstreamer.video.h264_encoders]] "
        "in your config.toml or your Gstreamer installation");
  }
  auto hevc_encoder = get_encoder("HEVC", default_gst_video_settings.hevc_encoders, vendor);
  auto av1_encoder = get_encoder("AV1", default_gst_video_settings.av1_encoders, vendor);

  /* Get paired clients */
  auto cfg_clients = toml::find<std::vector<PairedClient>>(cfg, "paired_clients");
  auto paired_clients =
      cfg_clients                                                                                             //
      | ranges::views::transform([](const PairedClient &client) { return immer::box<PairedClient>{client}; }) //
      | ranges::to<immer::vector<immer::box<PairedClient>>>();

  auto default_h264 = utils::get_optional(default_gst_encoder_settings, h264_encoder->plugin_name);
  auto default_hevc = utils::get_optional(default_gst_encoder_settings, hevc_encoder->plugin_name);
  auto default_av1 = utils::get_optional(default_gst_encoder_settings, av1_encoder->plugin_name);

  /* Get apps, here we'll merge the default gstreamer settings with the app specific overrides */
  auto cfg_apps = toml::find<std::vector<toml::value>>(cfg, "apps");
  auto apps =
      cfg_apps |                                                               //
      ranges::views::enumerate |                                               //
      ranges::views::transform([&](std::pair<int, const toml::value &> pair) { //
        auto [idx, item] = pair;
        auto app_title = toml::find<std::string>(item, "title");
        auto app_render_node = toml::find_or(item, "render_node", default_app_render_node);
        if (app_render_node != default_gst_render_node) {
          logs::log(logs::warning,
                    "App {} render node ({}) doesn't match the default GPU ({})",
                    app_title,
                    app_render_node,
                    default_gst_render_node);
          // TODO: allow user to override gst_render_node
        }

        auto h264_gst_pipeline =
            toml::find_or(item, "video", "source", default_gst_video_settings.default_source) + " !\n" +
            toml::find_or(
                item,
                "video",
                "video_params",
                h264_encoder->video_params.value_or(default_h264.value_or(GstEncoderDefault{}).video_params)) +
            " !\n" + toml::find_or(item, "video", "h264_encoder", h264_encoder->encoder_pipeline) + " !\n" +
            toml::find_or(item, "video", " sink ", default_gst_video_settings.default_sink);

        auto hevc_gst_pipeline =
            hevc_encoder.has_value()
                ? toml::find_or(item, "video", "source", default_gst_video_settings.default_source) + " !\n" +
                      toml::find_or(item,
                                    "video",
                                    "video_params",
                                    hevc_encoder->video_params.value_or(
                                        default_hevc.value_or(GstEncoderDefault{}).video_params)) +
                      " !\n" + toml::find_or(item, "video", "hevc_encoder", hevc_encoder->encoder_pipeline) + " !\n" +
                      toml::find_or(item, "video", " sink ", default_gst_video_settings.default_sink)
                : "";

        auto av1_gst_pipeline =
            av1_encoder.has_value()
                ? toml::find_or(item, "video", "source", default_gst_video_settings.default_source) + " !\n" +
                      toml::find_or(
                          item,
                          "video",
                          "video_params",
                          av1_encoder->video_params.value_or(default_av1.value_or(GstEncoderDefault{}).video_params)) +
                      " !\n" + toml::find_or(item, "video", "av1_encoder", av1_encoder->encoder_pipeline) + " !\n" +
                      toml::find_or(item, "video", " sink ", default_gst_video_settings.default_sink)
                : "";

        auto opus_gst_pipeline =
            toml::find_or(item, "audio", "source", default_gst_audio_settings.default_source) + " !\n" +
            toml::find_or(item, "audio", "video_params", default_gst_audio_settings.default_audio_params) + " !\n" +
            toml::find_or(item, "audio", "opus_encoder", default_gst_audio_settings.default_opus_encoder) + " !\n" +
            toml::find_or(item, "audio", "sink", default_gst_audio_settings.default_sink);

        auto joypad_type = utils::to_lower(toml::find_or(item, "joypad_type", "auto"s));
        moonlight::control::pkts::CONTROLLER_TYPE joypad_type_enum = moonlight::control::pkts::CONTROLLER_TYPE::AUTO;
        if (joypad_type == "xbox") {
          joypad_type_enum = moonlight::control::pkts::CONTROLLER_TYPE::XBOX;
        } else if (joypad_type == "nintendo") {
          joypad_type_enum = moonlight::control::pkts::CONTROLLER_TYPE::NINTENDO;
        } else if (joypad_type == "ps") {
          joypad_type_enum = moonlight::control::pkts::CONTROLLER_TYPE::PS;
        } else if (joypad_type != "auto") {
          logs::log(logs::warning, "Unknown joypad type: {}", joypad_type);
        }

        return events::App{.base = {.title = app_title,
                                    .id = std::to_string(idx + 1),
                                    .support_hdr = toml::find_or<bool>(item, "support_hdr", false)},
                           .h264_gst_pipeline = h264_gst_pipeline,
                           .hevc_gst_pipeline = hevc_gst_pipeline,
                           .av1_gst_pipeline = av1_gst_pipeline,
                           .render_node = app_render_node,

                           .opus_gst_pipeline = opus_gst_pipeline,
                           .start_virtual_compositor = toml::find_or<bool>(item, "start_virtual_compositor", true),
                           .runner = get_runner(item, ev_bus),
                           .joypad_type = joypad_type_enum};
      }) |                                      //
      ranges::to<immer::vector<events::App>>(); //

  auto clients_atom = std::make_shared<immer::atom<state::PairedClientList>>(paired_clients);
  return Config{.uuid = uuid,
                .hostname = hostname,
                .config_source = source,
                .support_hevc = hevc_encoder.has_value(),
                .support_av1 = av1_encoder.has_value() && encoder_type(*av1_encoder) != SOFTWARE,
                .paired_clients = clients_atom,
                .apps = apps};
}

void pair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients->update(
      [&client](const state::PairedClientList &paired_clients) { return paired_clients.push_back(client); });

  // Update TOML
  toml::value tml = toml::parse(cfg.config_source);
  tml.at("paired_clients").as_array().emplace_back(client);

  write(tml, cfg.config_source);
}

void unpair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients->update([&client](const state::PairedClientList &paired_clients) {
    return paired_clients                                               //
           | ranges::views::filter([&client](auto paired_client) {      //
               return paired_client->client_cert != client.client_cert; //
             })                                                         //
           | ranges::to<state::PairedClientList>();                     //
  });

  // Update TOML
  toml::value tml = toml::parse(cfg.config_source);

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