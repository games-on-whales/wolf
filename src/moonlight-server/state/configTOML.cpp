#include <algorithm>
#include <events/events.hpp>
#include <events/reflectors.hpp>
#include <fstream>
#include <gst/gstelementfactory.h>
#include <gst/gstregistry.h>
#include <platforms/hw.hpp>
#include <range/v3/view.hpp>
#include <rfl/toml.hpp>
#include <state/config.hpp>

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

static Encoder encoder_type(const GstEncoder &settings) {
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

std::optional<GstEncoder> get_encoder(std::string_view tech,
                                      std::string_view encoder_node,
                                      const std::vector<GstEncoder> &encoders,
                                      const GPU_VENDOR &vendor) {
  auto default_is_available = std::bind(is_available, vendor, std::placeholders::_1);
  auto encoder = std::find_if(encoders.begin(), encoders.end(), default_is_available);
  if (encoder != std::end(encoders)) {
    auto encoder_node_name = get_render_node_name(encoder_node);
    if (encoder_type(*encoder) == VAAPI && encoder_node_name != "renderD128") {
      auto possible_vaapi_plugin = fmt::format("va{}{}enc", encoder_node_name, tech);
      auto possible_encoder = GstEncoder{.plugin_name = encoder->plugin_name,
                                         .check_elements = {possible_vaapi_plugin, "vapostproc"},
                                         .video_params = encoder->video_params,
                                         .video_params_zero_copy = encoder->video_params_zero_copy,
                                         .encoder_pipeline = encoder->encoder_pipeline};
      logs::log(logs::debug, "Checking if {} is available", possible_vaapi_plugin);
      if (is_available(vendor, possible_encoder)) {
        possible_encoder.encoder_pipeline = std::regex_replace(possible_encoder.encoder_pipeline,
                                                               std::regex(fmt::format("va{}enc", tech)),
                                                               possible_vaapi_plugin);
        logs::log(logs::info, "Detected multiple VAAPI capable devices, using {} encoder", possible_vaapi_plugin);
        return possible_encoder;
      }
    }
    if (encoder_type(*encoder) == NVIDIA && encoder_node_name != "renderD128") {
      // TODO: do the same trick for Nvidia and nvh265device{dev-number}enc we have to get that dev-number though..
    }
    logs::log(logs::info, "Using {} encoder: {}", tech, encoder->plugin_name);
    if (encoder_type(*encoder) == SOFTWARE) {
      logs::log(logs::warning, "Software {} encoder detected", tech);
    }
    return *encoder;
  }
  return std::nullopt;
}

/**
 * ID is used by Moonlight to uniquely identify the app.
 * We have to change it if we change something that will be displayed
 */
std::string generate_app_id(const BaseApp &app) {
  auto hash = utils::hash(app.icon_png_path.value_or("") + app.title);
  // Value must be truncated to signed 32-bit range due to client limitations
  return std::to_string(abs((int32_t)hash));
}

std::shared_ptr<immer::atom<immer::vector<immer::box<events::App>>>>
parse_apps(const std::vector<BaseApp> &apps,
           const std::string &default_app_render_node,
           const std::string &default_gst_render_node,
           const BaseAppVideoOverride &default_video_settings,
           const std::string &h264_video_params,
           const std::string &hevc_video_params,
           const std::string &av1_video_params,
           const BaseAppAudioOverride &default_audio_settings,
           SessionsAtoms running_sessions,
           const std::shared_ptr<events::EventBusType> &ev_bus) {

  auto parsed_apps =
      apps |                                             //
      ranges::views::transform([&](const BaseApp &app) { //
        auto app_render_node = app.render_node.value_or(default_app_render_node);
        if (app_render_node != default_gst_render_node) {
          logs::log(logs::warning,
                    "App {} render node ({}) doesn't match the default GPU ({})",
                    app.title,
                    app_render_node,
                    default_gst_render_node);
          // TODO: allow user to override gst_render_node
        }
        auto app_video_settings = app.video.value_or(default_video_settings);
        auto app_audio_settings = app.audio.value_or(default_audio_settings);

        auto h264_gst_pipeline = fmt::format(
            "{} !\n{} !\n{} !\n{}", //
            app_video_settings.source.value_or(default_video_settings.source.value()),
            app_video_settings.video_params.value_or(h264_video_params),
            app_video_settings.h264_encoder.value_or(default_video_settings.h264_encoder.value()),
            app_video_settings.sink.value_or(default_video_settings.sink.value()));

        auto hevc_gst_pipeline =
            default_video_settings.hevc_encoder.has_value()
                ? fmt::format("{} !\n{} !\n{} !\n{}", //
                              app_video_settings.source.value_or(default_video_settings.source.value()),
                              app_video_settings.video_params.value_or(hevc_video_params),
                              app_video_settings.hevc_encoder.value_or(default_video_settings.hevc_encoder.value()),
                              app_video_settings.sink.value_or(default_video_settings.sink.value()))
                : "";

        auto av1_gst_pipeline =
            default_video_settings.av1_encoder.has_value()
                ? fmt::format("{} !\n{} !\n{} !\n{}", //
                              app_video_settings.source.value_or(default_video_settings.source.value()),
                              app_video_settings.video_params.value_or(av1_video_params),
                              app_video_settings.av1_encoder.value_or(default_video_settings.av1_encoder.value()),
                              app_video_settings.sink.value_or(default_video_settings.sink.value()))
                : "";

        auto opus_gst_pipeline = fmt::format(
            "{} !\n{} !\n{} !\n{}", //
            app_audio_settings.source.value_or(default_audio_settings.source.value()),
            app_audio_settings.audio_params.value_or(default_audio_settings.audio_params.value()),
            app_audio_settings.opus_encoder.value_or(default_audio_settings.opus_encoder.value()),
            app_audio_settings.sink.value_or(default_audio_settings.sink.value()));

        return immer::box<events::App>{
            events::App{.base = {.title = app.title,
                                 .id = generate_app_id(app),
                                 .support_hdr = false,
                                 .icon_png_path = app.icon_png_path},
                        .video_producer_buffer_caps = default_video_settings.producer_buffer_caps.value(),
                        .h264_gst_pipeline = h264_gst_pipeline,
                        .hevc_gst_pipeline = hevc_gst_pipeline,
                        .av1_gst_pipeline = av1_gst_pipeline,
                        .render_node = app_render_node,

                        .opus_gst_pipeline = opus_gst_pipeline,
                        .start_virtual_compositor = app.start_virtual_compositor.value_or(true),
                        .start_audio_server = app.start_audio_server.value_or(true),
                        .runner = get_runner(app.runner, ev_bus)}};
      }) |                                                  //
      ranges::to<immer::vector<immer::box<events::App>>>(); //

  return std::make_shared<immer::atom<immer::vector<immer::box<events::App>>>>(parsed_apps);
}

Config load_or_default(const std::string &source,
                       const std::shared_ptr<events::EventBusType> &ev_bus,
                       SessionsAtoms running_sessions) {
  if (!file_exist(source)) {
    logs::log(logs::warning, "Unable to open config file: {}, creating one using defaults", source);
    create_default(source);
  }

  // First check the version of the config file
  auto base_cfg = rfl::toml::load<BaseConfig, rfl::DefaultIfMissing>(source).value();
  auto version = base_cfg.config_version.value_or(0);
  if (version <= 6) {
    logs::log(logs::warning, "Found old config file (v{}), migrating to v7", version);
    auto backup = source + ".v" + std::to_string(version) + ".old";
    std::filesystem::rename(source, backup);
    auto old_cfg = toml::parse_file(backup);
    create_default(source);
    auto new_cfg = toml::parse_file(source);
    new_cfg.insert_or_assign("hostname", old_cfg.at("hostname"));
    new_cfg.insert_or_assign("uuid", old_cfg.at("uuid"));
    new_cfg.insert_or_assign("paired_clients", old_cfg.at("paired_clients"));
    if (old_cfg.contains("profiles")) {
      new_cfg.insert_or_assign("profiles", old_cfg.at("profiles"));
    } else if (old_cfg.contains("apps")) {
      auto moonlight_profile = new_cfg["profiles"].as_array()->at(0).as_table();
      new_cfg.insert_or_assign(
          "profiles",
          toml::array{*moonlight_profile,
                      toml::table({{"id", "user"}, {"name", "User"}, {"apps", old_cfg.at("apps")}})});
    }
    std::ofstream out_file;
    out_file.open(source);
    if (!out_file.is_open()) {
      throw std::runtime_error("Failed to open config file for writing");
    }
    out_file << new_cfg;
    out_file.close();
    logs::log(logs::debug, "Migrated config from v{} to v7", version);
  }

  // Will throw if the config is invalid
  auto cfg = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(source).value();

  auto default_gst_video_settings = cfg.gstreamer.video;
  auto default_gst_audio_settings = cfg.gstreamer.audio;
  if (default_gst_video_settings.default_source.find("name=interpipesrc") == std::string::npos) {
    logs::log(logs::debug, "Found interpipesrc without name, adding it");
    default_gst_video_settings.default_source =
        default_gst_video_settings.default_source.replace(0, 12, "interpipesrc name=interpipesrc_{}_video");
  }
  if (default_gst_audio_settings.default_source.find("name=interpipesrc") == std::string::npos) {
    logs::log(logs::debug, "Found interpipesrc without name, adding it");
    default_gst_audio_settings.default_source =
        default_gst_audio_settings.default_source.replace(0, 12, "interpipesrc name=interpipesrc_{}_audio");
  }

  auto default_gst_encoder_settings = default_gst_video_settings.defaults;
  bool use_zero_copy = utils::get_env("WOLF_USE_ZERO_COPY", "") != std::string("FALSE");

  auto default_app_render_node = utils::get_env("WOLF_RENDER_NODE", "/dev/dri/renderD128");
  auto default_gst_render_node = utils::get_env("WOLF_ENCODER_NODE", default_app_render_node);
  auto vendor = get_vendor(default_gst_render_node);
  if (vendor == GPU_VENDOR::UNKNOWN) {
    logs::log(logs::warning, "Unable to detect GPU vendor, disabling zero copy pipeline.");
    use_zero_copy = false;
  }

  /* Automatically pick the best encoders */
  auto h264_encoder = get_encoder("h264", default_gst_render_node, default_gst_video_settings.h264_encoders, vendor);
  if (!h264_encoder) {
    throw std::runtime_error(
        "Unable to find a compatible H.264 encoder, please check [[gstreamer.video.h264_encoders]] "
        "in your config.toml or your Gstreamer installation");
  }
  auto hevc_encoder = get_encoder("h265", default_gst_render_node, default_gst_video_settings.hevc_encoders, vendor);
  auto av1_encoder = get_encoder("av1", default_gst_render_node, default_gst_video_settings.av1_encoders, vendor);

  /* Get paired clients */
  auto paired_clients =
      cfg.paired_clients                                                                                      //
      | ranges::views::transform([](const PairedClient &client) { return immer::box<PairedClient>{client}; }) //
      | ranges::to<immer::vector<immer::box<PairedClient>>>();

  auto default_base_video = BaseAppVideoOverride{.source = default_gst_video_settings.default_source,
                                                 .sink = default_gst_video_settings.default_sink,
                                                 .producer_buffer_caps = "video/x-raw"};
  auto default_base_audio = BaseAppAudioOverride{.source = default_gst_audio_settings.default_source,
                                                 .audio_params = default_gst_audio_settings.default_audio_params,
                                                 .opus_encoder = default_gst_audio_settings.default_opus_encoder,
                                                 .sink = default_gst_audio_settings.default_sink};

  auto video_encoder = encoder_type(*h264_encoder);
  if (use_zero_copy) {
    switch (video_encoder) {
    case NVIDIA: {
      default_base_video.producer_buffer_caps = "video/x-raw(memory:CUDAMemory)";
      break;
    }
    case VAAPI:
    case QUICKSYNC: {
      auto sink_caps = gstreamer::get_dma_caps("vapostproc", GST_PAD_SINK);
      auto source_caps = gstreamer::get_dma_caps("waylanddisplaysrc", GST_PAD_SRC);
      logs::log(logs::debug, "Required DMA formats for vapostproc: {}", sink_caps);
      logs::log(logs::debug, "Available DMA formats for waylanddisplaysrc: {}", source_caps);
      auto gst_caps = source_caps | //
                      ranges::views::filter([&sink_caps](const std::string &cap) {
                        return std::find(sink_caps.begin(), sink_caps.end(), cap) != sink_caps.end();
                      }) | //
                      ranges::views::remove_if([](const std::string &cap) {
                        // TODO: HDR isn't supported by Wolf yet (so we remove P010 and AR30 format)
                        return cap.find("P010") != std::string::npos || cap.find("AR30") != std::string::npos ||
                               // We also remove formats that are padded with spaces since they need escaping
                               cap.find(" ") != std::string::npos;
                      }) | //
                      ranges::to<std::vector>();
      if (gst_caps.empty()) {
        logs::log(logs::warning,
                  "Unable to find any compatible DMA formats between waylanddisplaysrc and vapostproc, disabling "
                  "zero copy pipeline.");
        use_zero_copy = false;
      } else {
        default_base_video.producer_buffer_caps =
            fmt::format("video/x-raw(memory:DMABuf), drm-format={{{}}}", utils::join(gst_caps, ","));
      }
      break;
    }
    default: {
    }
    }
  }

  logs::log(logs::info,
            "Using {} pipeline on {} ({})",
            use_zero_copy ? "zero copy" : "legacy",
            get_vendor_name(vendor),
            default_gst_render_node);

  default_base_video.h264_encoder = h264_encoder.value().encoder_pipeline;
  if (hevc_encoder) {
    default_base_video.hevc_encoder = hevc_encoder.value().encoder_pipeline;
  } else {
    logs::log(logs::warning, "Unable to find an HEVC encoder, disabling it");
  }

  if (av1_encoder) {
    default_base_video.av1_encoder = av1_encoder.value().encoder_pipeline;
  } else {
    logs::log(logs::warning, "Unable to find an AV1 encoder, disabling it");
  }

  auto empty_enc = GstEncoderDefault{};
  auto default_h264 = utils::get_optional(default_gst_encoder_settings, h264_encoder.value_or(GstEncoder{}).plugin_name)
                          .value_or(empty_enc);
  auto default_hevc = utils::get_optional(default_gst_encoder_settings, hevc_encoder.value_or(GstEncoder{}).plugin_name)
                          .value_or(empty_enc);
  auto default_av1 = utils::get_optional(default_gst_encoder_settings, av1_encoder.value_or(GstEncoder{}).plugin_name)
                         .value_or(empty_enc);

  auto h264_video_params = use_zero_copy
                               ? h264_encoder->video_params_zero_copy.value_or(default_h264.video_params_zero_copy)
                               : h264_encoder->video_params.value_or(default_h264.video_params);

  std::string hevc_video_params;
  if (hevc_encoder) {
    hevc_video_params = use_zero_copy
                            ? hevc_encoder->video_params_zero_copy.value_or(default_hevc.video_params_zero_copy)
                            : hevc_encoder->video_params.value_or(default_hevc.video_params);
  }

  std::string av1_video_params;
  if (av1_encoder) {
    av1_video_params = use_zero_copy ? av1_encoder->video_params_zero_copy.value_or(default_av1.video_params_zero_copy)
                                     : av1_encoder->video_params.value_or(default_av1.video_params);
  }

  auto clients_atom = std::make_shared<immer::atom<PairedClientList>>(paired_clients);

  /* Get profiles, for each app defined will merge with default settings */
  auto profiles = cfg.profiles | //
                  ranges::views::transform([&](const Profile &profile) {
                    return events::Profile{.id = profile.id,
                                           .name = profile.name.value_or(""),
                                           .icon_png_path = profile.icon_png_path.value_or(""),
                                           .pin = profile.pin,
                                           .apps = parse_apps(profile.apps,
                                                              default_app_render_node,
                                                              default_gst_render_node,
                                                              default_base_video,
                                                              h264_video_params,
                                                              hevc_video_params,
                                                              av1_video_params,
                                                              default_base_audio,
                                                              running_sessions,
                                                              ev_bus)};
                  }) |
                  ranges::to<ProfilesList>();
  auto profiles_atom = std::make_shared<immer::atom<ProfilesList>>(profiles);

  return Config{.uuid = cfg.uuid,
                .hostname = cfg.hostname,
                .config_source = source,
                .support_hevc = hevc_encoder.has_value(),
                .support_av1 = av1_encoder.has_value() && encoder_type(*av1_encoder) != SOFTWARE,
                .paired_clients = clients_atom,
                .profiles = profiles_atom};
}

void pair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients->update([&client](const PairedClientList &paired_clients) {
    // Removing the client if already present (see: https://github.com/games-on-whales/wolf/issues/211)
    auto filtered_clients = paired_clients                                               //
                            | ranges::views::filter([&client](auto paired_client) {      //
                                return paired_client->client_cert != client.client_cert; //
                              })                                                         //
                            | ranges::to<PairedClientList>();                            //
    return filtered_clients.push_back(client);
  });

  // Update TOML
  auto tml = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(cfg.config_source).value();
  tml.paired_clients.push_back(client);
  rfl::toml::save(cfg.config_source, tml);
}

void unpair(const Config &cfg, const PairedClient &client) {
  // Update CFG
  cfg.paired_clients->update([&client](const PairedClientList &paired_clients) {
    return paired_clients                                               //
           | ranges::views::filter([&client](auto paired_client) {      //
               return paired_client->client_cert != client.client_cert; //
             })                                                         //
           | ranges::to<PairedClientList>();                            //
  });

  // Update TOML
  auto tml = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(cfg.config_source).value();
  tml.paired_clients.erase(std::remove_if(tml.paired_clients.begin(),
                                          tml.paired_clients.end(),
                                          [&client](const auto &v) { return v.client_cert == client.client_cert; }),
                           tml.paired_clients.end());
  rfl::toml::save(cfg.config_source, tml);
}

void update_client_settings(const Config &cfg, std::size_t client_id, const PairedClient &updated_client) {

  auto update_client_fn = [&](immer::box<PairedClient> client) -> immer::box<PairedClient> {
    if (get_client_id(client) == client_id) {
      return immer::box<PairedClient>(updated_client);
    }
    return client;
  };

  cfg.paired_clients->update([&](const PairedClientList &paired_clients) {
    return paired_clients |                             //
           ranges::views::transform(update_client_fn) | //
           ranges::to<PairedClientList>();
  });

  // Update the TOML file
  auto tml = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(cfg.config_source).value();

  tml.paired_clients = tml.paired_clients |                         //
                       ranges::views::transform(update_client_fn) | //
                       ranges::to<std::vector<PairedClient>>();

  // Save back to file
  rfl::toml::save(cfg.config_source, tml);
}

void update_profiles(const Config &cfg, const ProfilesList &profiles) {
  cfg.profiles->store(profiles);

  auto tml = rfl::toml::load<WolfConfig, rfl::DefaultIfMissing>(cfg.config_source).value();
  tml.profiles = profiles | //
                 ranges::views::transform([](const immer::box<events::Profile> &p) {
                   return Profile{
                       .id = p->id,
                       .name = p->name,
                       .icon_png_path = p->icon_png_path,
                       .pin = p->pin,
                       .apps = p->apps->load().get() | //
                               ranges::views::transform([](const immer::box<events::App> &app) {
                                 return BaseApp{.title = app->base.title,
                                                .icon_png_path = app->base.icon_png_path,
                                                .render_node = app->render_node,
                                                .start_virtual_compositor = app->start_virtual_compositor,
                                                .start_audio_server = app->start_audio_server,
                                                .runner = app->runner->serialize()};
                               }) | //
                               ranges::to_vector,
                   };
                 }) | //
                 ranges::to_vector;
  rfl::toml::save(cfg.config_source, tml);
}

} // namespace state
