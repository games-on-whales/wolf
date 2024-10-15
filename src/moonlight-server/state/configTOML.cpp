#include <events/events.hpp>
#include <events/reflectors.hpp>
#include <fstream>
#include <gst/gstelementfactory.h>
#include <gst/gstregistry.h>
#include <platforms/hw.hpp>
#include <range/v3/view.hpp>
#include <rfl/toml.hpp>
#include <state/config.hpp>
#include <utility>

namespace state {

/**
 * A bit of magic here, it'll load up the default/config.toml via Cmake (look for `make_includable`)
 */
constexpr char const *default_toml =
#include "default/config.include.toml"
    ;

using namespace std::literals;
using namespace wolf::config;

void create_default(const std::string &source) {
  std::ofstream out_file;
  out_file.open(source);
  out_file << "# A unique identifier for this host" << std::endl;
  out_file << "uuid = \"" << gen_uuid() << "\"" << std::endl;
  out_file << default_toml;
  out_file.close();
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

Config load_or_default(const std::string &source,
                       const std::shared_ptr<events::EventBusType> &ev_bus,
                       state::SessionsAtoms running_sessions) {
  if (!file_exist(source)) {
    logs::log(logs::warning, "Unable to open config file: {}, creating one using defaults", source);
    create_default(source);
  }

  // First check the version of the config file
  auto base_cfg = rfl::toml::load<BaseConfig, rfl::DefaultIfMissing>(source).value();
  auto version = base_cfg.config_version.value_or(0);
  if (version <= 3) {
    logs::log(logs::warning, "Found old config file, migrating to newer version");

    std::filesystem::rename(source, source + ".v3.old");
    auto v3 = toml::parse_file(source + ".v3.old");
    create_default(source);
    auto v4 = toml::parse_file(source);
    // Copy back everything apart from the Gstreamer pipelines
    v4.insert_or_assign("hostname", v3.at("hostname"));
    v4.insert_or_assign("uuid", v3.at("uuid"));
    v4.insert_or_assign("apps", v3.at("apps"));
    v4.insert_or_assign("paired_clients", v3.at("paired_clients"));

    std::ofstream out_file;
    out_file.open(source);
    out_file << v4;
    out_file.close();
  }

  // Will throw if the config is invalid
  auto cfg = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(source).value();

  auto default_gst_video_settings = cfg.gstreamer.video;
  if (default_gst_video_settings.default_source.find("appsrc") != std::string::npos) {
    logs::log(logs::debug, "Found appsrc in default_source, migrating to interpipesrc");
    default_gst_video_settings.default_source = "interpipesrc listen-to={session_id}_video is-live=true "
                                                "stream-sync=restart-ts max-bytes=0 max-buffers=3 block=false";
  }

  auto default_gst_audio_settings = cfg.gstreamer.audio;
  if (default_gst_audio_settings.default_source.find("appsrc") != std::string::npos) {
    logs::log(logs::debug, "Found pulsesrc in default_source, migrating to interpipesrc");
    default_gst_audio_settings.default_source = "interpipesrc listen-to={session_id}_audio is-live=true "
                                                "stream-sync=restart-ts max-bytes=0 max-buffers=3 block=false";
  }
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
  auto paired_clients =
      cfg.paired_clients                                                                                      //
      | ranges::views::transform([](const PairedClient &client) { return immer::box<PairedClient>{client}; }) //
      | ranges::to<immer::vector<immer::box<PairedClient>>>();

  auto default_h264 =
      utils::get_optional(default_gst_encoder_settings, h264_encoder.value_or(GstEncoder{}).plugin_name);
  auto default_hevc =
      utils::get_optional(default_gst_encoder_settings, hevc_encoder.value_or(GstEncoder{}).plugin_name);
  auto default_av1 = utils::get_optional(default_gst_encoder_settings, av1_encoder.value_or(GstEncoder{}).plugin_name);

  auto default_base_video = BaseAppVideoOverride{};

  /* Get apps, here we'll merge the default gstreamer settings with the app specific overrides */
  auto apps =
      cfg.apps |                                                     //
      ranges::views::enumerate |                                     //
      ranges::views::transform([&](std::pair<int, BaseApp &> pair) { //
        auto [idx, app] = pair;
        auto app_render_node = app.render_node.value_or(default_app_render_node);
        if (app_render_node != default_gst_render_node) {
          logs::log(logs::warning,
                    "App {} render node ({}) doesn't match the default GPU ({})",
                    app.title,
                    app_render_node,
                    default_gst_render_node);
          // TODO: allow user to override gst_render_node
        }

        auto h264_gst_pipeline = fmt::format(
            "{} !\n{} !\n{} !\n{}", //
            app.video.value_or(default_base_video).source.value_or(default_gst_video_settings.default_source),
            app.video.value_or(default_base_video)
                .video_params.value_or(
                    h264_encoder->video_params.value_or(default_h264.value_or(GstEncoderDefault{}).video_params)),
            app.video.value_or(default_base_video).h264_encoder.value_or(h264_encoder->encoder_pipeline),
            app.video.value_or(default_base_video).sink.value_or(default_gst_video_settings.default_sink));

        auto hevc_gst_pipeline =
            hevc_encoder.has_value()
                ? fmt::format(
                      "{} !\n{} !\n{} !\n{}", //
                      app.video.value_or(default_base_video).source.value_or(default_gst_video_settings.default_source),
                      app.video.value_or(default_base_video)
                          .video_params.value_or(hevc_encoder->video_params.value_or(
                              default_hevc.value_or(GstEncoderDefault{}).video_params)),
                      app.video.value_or(default_base_video).hevc_encoder.value_or(hevc_encoder->encoder_pipeline),
                      app.video.value_or(default_base_video).sink.value_or(default_gst_video_settings.default_sink))
                : "";

        auto av1_gst_pipeline =
            av1_encoder.has_value()
                ? fmt::format(
                      "{} !\n{} !\n{} !\n{}", //
                      app.video.value_or(default_base_video).source.value_or(default_gst_video_settings.default_source),
                      app.video.value_or(default_base_video)
                          .video_params.value_or(av1_encoder->video_params.value_or(
                              default_av1.value_or(GstEncoderDefault{}).video_params)),
                      app.video.value_or(default_base_video).av1_encoder.value_or(av1_encoder->encoder_pipeline),
                      app.video.value_or(default_base_video).sink.value_or(default_gst_video_settings.default_sink))
                : "";

        auto opus_gst_pipeline = fmt::format(
            "{} !\n{} !\n{} !\n{}", //
            app.audio.value_or(BaseAppAudioOverride{}).source.value_or(default_gst_audio_settings.default_source),
            app.audio.value_or(BaseAppAudioOverride{})
                .audio_params.value_or(default_gst_audio_settings.default_audio_params),
            app.audio.value_or(BaseAppAudioOverride{})
                .opus_encoder.value_or(default_gst_audio_settings.default_opus_encoder),
            app.audio.value_or(BaseAppAudioOverride{}).sink.value_or(default_gst_audio_settings.default_sink));

        return immer::box<events::App>{
            events::App{.base = {.title = app.title,
                                 .id = std::to_string(idx + 1),
                                 .support_hdr = false,
                                 .icon_png_path = app.icon_png_path},
                        .h264_gst_pipeline = h264_gst_pipeline,
                        .hevc_gst_pipeline = hevc_gst_pipeline,
                        .av1_gst_pipeline = av1_gst_pipeline,
                        .render_node = app_render_node,

                        .opus_gst_pipeline = opus_gst_pipeline,
                        .start_virtual_compositor = app.start_virtual_compositor.value_or(true),
                        .start_audio_server = app.start_audio_server.value_or(true),
                        .runner = get_runner(app.runner, ev_bus, running_sessions)}};
      }) |                                                  //
      ranges::to<immer::vector<immer::box<events::App>>>(); //

  auto clients_atom = std::make_shared<immer::atom<state::PairedClientList>>(paired_clients);
  auto apps_atom = std::make_shared<immer::atom<immer::vector<immer::box<events::App>>>>(apps);
  return Config{.uuid = cfg.uuid,
                .hostname = cfg.hostname,
                .config_source = source,
                .support_hevc = hevc_encoder.has_value(),
                .support_av1 = av1_encoder.has_value() && encoder_type(*av1_encoder) != SOFTWARE,
                .paired_clients = clients_atom,
                .apps = apps_atom};
}

void pair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients->update(
      [&client](const state::PairedClientList &paired_clients) { return paired_clients.push_back(client); });

  // Update TOML
  auto tml = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(cfg.config_source).value();
  tml.paired_clients.push_back(client);
  rfl::toml::save(cfg.config_source, tml);
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
  auto tml = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(cfg.config_source).value();
  tml.paired_clients.erase(std::remove_if(tml.paired_clients.begin(),
                                          tml.paired_clients.end(),
                                          [&client](const auto &v) { return v.client_cert == client.client_cert; }),
                           tml.paired_clients.end());
  rfl::toml::save(cfg.config_source, tml);
}

} // namespace state