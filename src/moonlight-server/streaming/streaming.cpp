#include "platforms/hw.hpp"

#include <chrono>
#include <control/control.hpp>
#include <core/batched_send.hpp>
#include <gst-video-context.hpp>
#include <gstreamer-1.0/gst/app/gstappsink.h>
#include <gstreamer-1.0/gst/app/gstappsrc.h>
#include <helpers/utils.hpp>
#include <immer/array.hpp>
#include <immer/box.hpp>
#include <memory>
#include <streaming/streaming.hpp>
#include <thread>

namespace streaming {

using namespace wolf::core::gstreamer;
using namespace wolf::core;

struct GstBusData {
  std::shared_ptr<boost::promise<WaylandDisplayReady>> on_ready;
  gst_element_ptr wayland_plugin;
};

gboolean structure_each(GQuark field_id, const GValue *value, gpointer user_data) {
  auto field_str = std::string(g_quark_to_string(field_id));
  if (!G_VALUE_HOLDS_STRING(value)) {
    logs::log(logs::warning, "Wayland source message: {} = {}", field_str, "not a string");
    return FALSE;
  }
  auto value_str = g_value_get_string(value);
  logs::log(logs::debug, "Wayland source message: {} = {}", field_str, value_str);

  if (field_str == "WAYLAND_DISPLAY") {
    logs::log(logs::info, "Wayland display ready, listening on: {}", value_str);
    auto bus_data = static_cast<GstBusData *>(user_data);
    bus_data->on_ready->set_value(
        WaylandDisplayReady{.wayland_socket_name = value_str, .wayland_plugin = bus_data->wayland_plugin});
  }

  return TRUE;
}

static void application_message_handler(GstBus *bus, GstMessage *msg, gpointer data) {
  auto structure = gst_message_get_structure(msg);
  if (gst_structure_has_name(structure, "wayland.src")) {
    gst_structure_foreach(structure, structure_each, data);
  }
}

struct NeedContextData {
  const std::string device_path;
  std::shared_ptr<immer::atom<gst_video_context::gst_context_ptr>> gst_context;
};

static void need_context_handler(GstBus *bus, GstMessage *msg, gpointer data) {
  auto ctx_data = static_cast<NeedContextData *>(data);
  if (auto gst_context = ctx_data->gst_context->load().get()) {
    logs::log(logs::debug, "Context already set, passing it to the pipeline.");
    gst_video_context::set_context(gst_context, msg);
  } else if (auto video_context = gst_video_context::need_context_for_device(ctx_data->device_path, msg)) {
    ctx_data->gst_context->store(video_context);
  }
}

static GstBusSyncReply bus_sync_handler(GstBus *bus, GstMessage *msg, gpointer data) {
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_NEED_CONTEXT) {
    need_context_handler(bus, msg, data);
  }
  return GST_BUS_PASS;
}

std::pair<std::string, std::string> get_color_params(immer::box<events::VideoSession> video_session) {
  std::string color_range = (video_session->color_range == events::ColorRange::JPEG) ? "jpeg" : "mpeg2";
  std::string color_space;
  switch (video_session->color_space) {
  case events::ColorSpace::BT601:
    color_space = "bt601";
    break;
  case events::ColorSpace::BT709:
    color_space = "bt709";
    break;
  case events::ColorSpace::BT2020:
    color_space = "bt2020";
    break;
  }
  return std::make_pair(color_range, color_space);
}

void start_video_producer(const std::string &session_id,
                          const std::string &buffer_format,
                          const std::string &render_node,
                          const wolf::core::virtual_display::DisplayMode &display_mode,
                          std::shared_ptr<immer::atom<gst_video_context::gst_context_ptr>> video_context,
                          std::shared_ptr<boost::promise<WaylandDisplayReady>> on_ready,
                          std::shared_ptr<events::EventBusType> event_bus) {
  auto pipeline = fmt::format("waylanddisplaysrc name=wolf_wayland_source render_node={render_node} ! "
                              "{buffer_format}, width={width}, height={height}, framerate={fps}/1 ! \n"    //
                              "interpipesink sync=true async=false name={session_id}_video max-buffers=1", //
                              fmt::arg("buffer_format", buffer_format),
                              fmt::arg("render_node", render_node),
                              fmt::arg("session_id", session_id),
                              fmt::arg("width", display_mode.width),
                              fmt::arg("height", display_mode.height),
                              fmt::arg("fps", display_mode.refreshRate));
  logs::log(logs::debug, "[GSTREAMER] Starting video producer: {}", pipeline);
  auto bus_data_ptr =
      std::make_shared<GstBusData>(GstBusData{.on_ready = std::move(on_ready), .wayland_plugin = nullptr});
  std::shared_ptr<NeedContextData> ctx_data_ptr =
      std::make_shared<NeedContextData>(NeedContextData{.device_path = render_node, .gst_context = video_context});
  run_pipeline(pipeline, [=](auto pipeline) {
    logs::log(logs::debug, "Setting up waylanddisplaysrc");

    auto wayland_plugin_el = gst_bin_get_by_name(GST_BIN(pipeline.get()), "wolf_wayland_source");
    auto wayland_plugin_ptr = gst_element_ptr(wayland_plugin_el, ::gst_object_unref);
    bus_data_ptr->wayland_plugin.swap(wayland_plugin_ptr);

    auto bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline.get()));
    g_signal_connect(bus, "message::application", G_CALLBACK(application_message_handler), bus_data_ptr.get());
    gst_bus_set_sync_handler(bus, bus_sync_handler, ctx_data_ptr.get(), nullptr);
    gst_object_unref(bus);

    auto stop_handler = event_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [session_id, pipeline](const immer::box<events::StopStreamEvent> &ev) {
          if (std::to_string(ev->session_id) == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping video producer: {}", session_id);
            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    auto stop_lobby_handler = event_bus->register_handler<immer::box<events::StopLobbyEvent>>(
        [session_id, pipeline](const immer::box<events::StopLobbyEvent> &ev) {
          if (ev->lobby_id == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping video producer: {}", session_id);
            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    return immer::array<immer::box<events::EventBusHandlers>>{std::move(stop_handler), std::move(stop_lobby_handler)};
  });
}

void start_audio_producer(const std::string &session_id,
                          const std::shared_ptr<events::EventBusType> &event_bus,
                          int channel_count,
                          const std::string &sink_name,
                          const std::string &server_name) {
  std::string channel_mask;
  switch (channel_count) {
  case 2:
    channel_mask = "0x3";
    break;
  case 6:
    channel_mask = "0x3f";
    break;
  case 8:
    channel_mask = "0xc3f";
    break;
  default:
    channel_mask = "";
  }

  auto pipeline = fmt::format("pulsesrc device=\"{sink_name}\" server=\"{server_name}\" ! "                           //
                              "audio/x-raw, channels={channels}, channel-mask=(bitmask){channel_mask}, rate=48000 ! " //
                              "queue leaky=downstream max-size-buffers=3 ! "                                          //
                              "interpipesink name=\"{session_id}_audio\" sync=true async=false max-buffers=3",
                              fmt::arg("session_id", session_id),
                              fmt::arg("channels", channel_count),
                              fmt::arg("channel_mask", channel_mask),
                              fmt::arg("sink_name", sink_name),
                              fmt::arg("server_name", server_name));
  logs::log(logs::debug, "[GSTREAMER] Starting audio producer: {}", pipeline);

  run_pipeline(pipeline, [=](auto pipeline) {
    auto stop_handler = event_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [session_id, pipeline](const immer::box<events::StopStreamEvent> &ev) {
          if (std::to_string(ev->session_id) == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping audio producer: {}", session_id);
            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    auto stop_lobby_handler = event_bus->register_handler<immer::box<events::StopLobbyEvent>>(
        [session_id, pipeline](const immer::box<events::StopLobbyEvent> &ev) {
          if (ev->lobby_id == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping video producer: {}", session_id);
            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    return immer::array<immer::box<events::EventBusHandlers>>{std::move(stop_handler), std::move(stop_lobby_handler)};
  });
}

namespace custom_sink {

struct PacingConfig {
  bool enabled = false;
  std::size_t max_packets_per_ms = 0;
  /**
   * Maximum number of packets per sendmmsg() syscall.
   * Caps batch size to stay under 64KB per call, following Sunshine's pattern.
   * Computed at runtime as min(16, 65536 / packet_size).
   * Does not affect pacing rate — only syscall granularity.
   */
  std::size_t max_batch_size = 16;
  std::chrono::steady_clock::time_point next_frame_start = std::chrono::steady_clock::now();
};

struct UDPSink {
  std::shared_ptr<udp::socket> socket;
  std::shared_ptr<udp::endpoint> client_endpoint;
  PacingConfig pacing;
  wolf::platform::batched_send_info_t send_info;
};

static void ensure_socket_open(UDPSink *udp_sink, bool is_video) {
  if (!udp_sink->socket->is_open()) {
    logs::log(logs::warning, "UDP Socket is not open");
    udp_sink->socket->open(udp::v4());
    wolf::platform::configure_socket_for_streaming(*udp_sink->socket, is_video);
    wolf::platform::enable_socket_qos(udp_sink->socket->native_handle(), is_video);
  }
}

static GstFlowReturn send_buffer_batched(GstBufferList *buffer_list, UDPSink *udp_sink) {
  guint num_buffers = gst_buffer_list_length(buffer_list);
  if (num_buffers == 0) {
    return GST_FLOW_OK;
  }

  ensure_socket_open(udp_sink, true);

  std::vector<std::pair<GstBuffer *, GstMapInfo>> mapped_buffers;
  udp_sink->send_info.payload_buffers.resize(num_buffers);
  mapped_buffers.reserve(num_buffers);

  for (guint i = 0; i < num_buffers; i++) {
    GstBuffer *buffer = gst_buffer_list_get(buffer_list, i);

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      logs::log(logs::error, "Failed to map buffer {} in batch", i);
      for (auto &[mapped_buffer, m] : mapped_buffers) {
        gst_buffer_unmap(mapped_buffer, &m);
      }
      return GST_FLOW_ERROR;
    }
    mapped_buffers.emplace_back(buffer, map);
    udp_sink->send_info.payload_buffers[i] =
        wolf::platform::buffer_descriptor_t(reinterpret_cast<const char *>(map.data), map.size);
  }

  udp_sink->send_info.native_socket = udp_sink->socket->native_handle();
  udp_sink->send_info.target_address = udp_sink->client_endpoint->address();
  udp_sink->send_info.target_port = udp_sink->client_endpoint->port();

  bool success = true;

  if (!udp_sink->pacing.enabled || num_buffers <= udp_sink->pacing.max_batch_size) {
    udp_sink->send_info.block_offset = 0;
    udp_sink->send_info.block_count = num_buffers;
    success = wolf::platform::send_batch(udp_sink->send_info);
  } else {
    auto &pacing = udp_sink->pacing;
    auto frame_start = std::max(pacing.next_frame_start, std::chrono::steady_clock::now());
    std::size_t packets_sent = 0;
    std::size_t packets_in_window = 0;

    while (packets_sent < num_buffers) {
      std::size_t remaining = num_buffers - packets_sent;
      std::size_t budget = pacing.max_packets_per_ms - packets_in_window;

      if (budget == 0) {
        auto due = frame_start + std::chrono::nanoseconds(1000000) * packets_sent / pacing.max_packets_per_ms;
        auto now = std::chrono::steady_clock::now();
        if (now < due) {
          std::this_thread::sleep_until(due);
        }
        packets_in_window = 0;
        continue;
      }

      std::size_t batch = std::min({budget, pacing.max_batch_size, remaining});

      udp_sink->send_info.block_offset = packets_sent;
      udp_sink->send_info.block_count = batch;
      if (!wolf::platform::send_batch(udp_sink->send_info)) {
        success = false;
        break;
      }

      packets_sent += batch;
      packets_in_window += batch;
    }

    pacing.next_frame_start = frame_start +
                              std::chrono::nanoseconds(1000000) * packets_sent / pacing.max_packets_per_ms;
  }

  for (auto &[buffer, m] : mapped_buffers) {
    gst_buffer_unmap(buffer, &m);
  }

  if (!success) {
    logs::log(logs::warning, "Failed to send batch of {} packets", num_buffers);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn send_buffer_single(GstBuffer *buffer, UDPSink *udp_sink) {
  GstMapInfo map;
  if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    ensure_socket_open(udp_sink, false);

    wolf::platform::batched_send_info_t send_info;
    send_info.payload_buffers.emplace_back(reinterpret_cast<const char *>(map.data), map.size);
    send_info.block_offset = 0;
    send_info.block_count = 1;
    send_info.native_socket = udp_sink->socket->native_handle();
    send_info.target_address = udp_sink->client_endpoint->address();
    send_info.target_port = udp_sink->client_endpoint->port();

    bool success = wolf::platform::send_batch(send_info);
    gst_buffer_unmap(buffer, &map);

    if (!success) {
      logs::log(logs::error, "Error sending UDP packet");
      return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
  } else {
    logs::log(logs::error, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn on_new_sample(GstAppSink *appsink, gpointer user_data) {
  std::shared_ptr<GstSample> sample(gst_app_sink_pull_sample(appsink), gst_sample_unref);
  if (!sample) {
    logs::log(logs::warning, "Custom sink: failed to create sample");
    return GST_FLOW_ERROR;
  }

  UDPSink *udp_sink = static_cast<UDPSink *>(user_data);

  if (GstBufferList *buffer_list = gst_sample_get_buffer_list(sample.get())) {
    return send_buffer_batched(buffer_list, udp_sink);
  } else if (GstBuffer *buffer = gst_sample_get_buffer(sample.get())) {
    return send_buffer_single(buffer, udp_sink);
  } else {
    logs::log(logs::warning, "Custom sink: failed to get buffer");
    return GST_FLOW_ERROR;
  }
}

static void configure_appsink(GstElement *appsink, UDPSink *udp_sink) {
  g_object_set(appsink, "emit-signals", FALSE, NULL);
  g_object_set(appsink, "buffer-list", TRUE, NULL);

  GstAppSinkCallbacks callbacks = {nullptr};
  callbacks.new_sample = on_new_sample;
  gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, udp_sink, nullptr);
}
} // namespace custom_sink

/**
 * Start VIDEO pipeline
 */
void start_streaming_video(immer::box<events::VideoSession> video_session,
                           const std::shared_ptr<events::EventBusType> &event_bus,
                           std::string client_ip,
                           unsigned short client_port,
                           std::shared_ptr<immer::atom<gst_video_context::gst_context_ptr>> video_context,
                           std::shared_ptr<udp::socket> video_socket) {
  auto [color_range, color_space] = get_color_params(video_session);

  auto pipeline = fmt::format(
      fmt::runtime(video_session->gst_pipeline),
      fmt::arg("session_id", video_session->session_id),
      fmt::arg("width", video_session->display_mode.width),
      fmt::arg("height", video_session->display_mode.height),
      fmt::arg("fps", video_session->display_mode.refreshRate),
      fmt::arg("bitrate", video_session->bitrate_kbps),
      fmt::arg("client_port", client_port),
      fmt::arg("client_ip", client_ip),
      fmt::arg("payload_size", video_session->packet_size),
      fmt::arg("fec_percentage", video_session->fec_percentage),
      fmt::arg("min_required_fec_packets", video_session->min_required_fec_packets),
      fmt::arg("slices_per_frame", video_session->slices_per_frame),
      fmt::arg("vbv_buffer_size", video_session->bitrate_kbps / video_session->display_mode.refreshRate),
      fmt::arg("color_space", color_space),
      fmt::arg("color_range", color_range),
      fmt::arg("host_port", video_session->port));
  logs::log(logs::debug, "Starting video pipeline: \n{}", pipeline);

  bool enable_pacing = utils::get_env("WOLF_ENABLE_VIDEO_PACING", "TRUE") == std::string("TRUE");
  std::shared_ptr<custom_sink::UDPSink> udp_sink = std::make_shared<custom_sink::UDPSink>(custom_sink::UDPSink{
      .socket = video_socket,
      .client_endpoint = std::make_shared<udp::endpoint>(boost::asio::ip::make_address(client_ip), client_port),
      .pacing = {
          .enabled = enable_pacing,
          .max_packets_per_ms = static_cast<std::size_t>(
              std::max(1L, static_cast<long>(1000000000L * 80 / 100 / 1000 / (video_session->packet_size * 8)))),
          .max_batch_size = std::min<std::size_t>(16, 65536 / video_session->packet_size),
      }});
  std::shared_ptr<NeedContextData> ctx_data_ptr = std::make_shared<NeedContextData>(
      NeedContextData{.device_path = video_session->render_node, .gst_context = video_context});
  run_pipeline(pipeline, [video_session, event_bus, udp_sink, ctx_data_ptr](auto pipeline) {
    if (auto app_sink_el = gst_bin_get_by_name(GST_BIN(pipeline.get()), "wolf_udp_sink")) {
      logs::log(logs::debug, "Setting up wolf_udp_sink");
      g_assert(GST_IS_APP_SINK(app_sink_el));
      configure_appsink(app_sink_el, udp_sink.get());
      gst_object_unref(app_sink_el);
    }

    auto bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline.get()));
    gst_bus_set_sync_handler(bus, bus_sync_handler, ctx_data_ptr.get(), nullptr);
    gst_object_unref(bus);

    /*
     * The force IDR event will be triggered by the control stream.
     * We have to pass this back into the gstreamer pipeline
     * in order to force the encoder to produce a new IDR packet
     */
    auto idr_handler = event_bus->register_handler<immer::box<events::IDRRequestEvent>>(
        [sess_id = video_session->session_id, pipeline](const immer::box<events::IDRRequestEvent> &ctrl_ev) {
          if (ctrl_ev->session_id == sess_id) {
            logs::log(logs::debug, "[GSTREAMER] Forcing IDR");
            // Force IDR event, see: https://github.com/centricular/gstwebrtc-demos/issues/186
            // https://gstreamer.freedesktop.org/documentation/additional/design/keyframe-force.html?gi-language=c
            wolf::core::gstreamer::send_message(
                pipeline.get(),
                gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, TRUE, NULL));
          }
        });

    auto pause_handler = event_bus->register_handler<immer::box<events::PauseStreamEvent>>(
        [sess_id = video_session->session_id, pipeline](const immer::box<events::PauseStreamEvent> &ev) {
          if (ev->session_id == sess_id) {
            logs::log(logs::debug, "[GSTREAMER] Pausing pipeline: {}", sess_id);

            /**
             * Unfortunately here we can't just pause the pipeline,
             * when a pipeline will be resumed there are a lot of breaking changes
             * like:
             *  - Client IP:PORT
             *  - AES key and IV for encrypted payloads
             *  - Client resolution, framerate, and encoding
             *
             *  The only solution is to kill the pipeline and re-create it again
             * when a resume happens
             */

            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    auto switch_producer_handler = event_bus->register_handler<immer::box<events::SwitchStreamProducerEvents>>(
        [sess_id = video_session->session_id,
         pipeline](const immer::box<events::SwitchStreamProducerEvents> &switch_ev) {
          if (switch_ev->session_id == sess_id) {
            logs::log(logs::debug,
                      "[GSTREAMER] Switching video producer pipeline for {} to {}",
                      sess_id,
                      switch_ev->interpipe_src_id);
            /* Grab a reference to the interpipesrc */
            auto pipe_name = fmt::format("interpipesrc_{}_video", sess_id);
            if (auto src = gst_bin_get_by_name(GST_BIN(pipeline.get()), pipe_name.c_str())) {
              /* Perform the switch */
              auto video_interpipe = fmt::format("{}_video", switch_ev->interpipe_src_id);
              g_object_set(src, "listen-to", video_interpipe.c_str(), nullptr);
              gst_object_unref(src);
            } else {
              logs::log(logs::error, "[GSTREAMER] Failed to get video interpipesrc for {}", sess_id);
            }
          }
        });

    auto stop_handler = event_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [sess_id = video_session->session_id, pipeline](const immer::box<events::StopStreamEvent> &ev) {
          if (ev->session_id == sess_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping pipeline: {}", sess_id);
            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    return immer::array<immer::box<events::EventBusHandlers>>{std::move(idr_handler),
                                                              std::move(pause_handler),
                                                              std::move(switch_producer_handler),
                                                              std::move(stop_handler)};
  });
}

/**
 * Start AUDIO pipeline
 */
void start_streaming_audio(immer::box<events::AudioSession> audio_session,
                           const std::shared_ptr<events::EventBusType> &event_bus,
                           std::string client_ip,
                           unsigned short client_port,
                           std::shared_ptr<udp::socket> audio_socket,
                           const std::string &sink_name,
                           const std::string &server_name) {
  auto pipeline = fmt::format(
      fmt::runtime(audio_session->gst_pipeline),
      fmt::arg("session_id", audio_session->session_id),
      fmt::arg("channels", audio_session->audio_mode.channels),
      fmt::arg("bitrate", audio_session->audio_mode.bitrate),
      // TODO: opusenc hardcodes those two
      // https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/1.24.6/subprojects/gst-plugins-base/ext/opus/gstopusenc.c#L661-666
      fmt::arg("streams", audio_session->audio_mode.streams),
      fmt::arg("coupled_streams", audio_session->audio_mode.coupled_streams),
      fmt::arg("sink_name", sink_name),
      fmt::arg("server_name", server_name),
      fmt::arg("packet_duration", audio_session->packet_duration),
      fmt::arg("aes_key", audio_session->aes_key),
      fmt::arg("aes_iv", audio_session->aes_iv),
      fmt::arg("encrypt", audio_session->encrypt_audio),
      fmt::arg("client_port", client_port),
      fmt::arg("client_ip", client_ip),
      fmt::arg("host_port", audio_session->port));
  logs::log(logs::debug, "Starting audio pipeline: \n{}", pipeline);

  std::shared_ptr<custom_sink::UDPSink> udp_sink = std::make_shared<custom_sink::UDPSink>(custom_sink::UDPSink{
      .socket = audio_socket,
      .client_endpoint = std::make_shared<udp::endpoint>(boost::asio::ip::make_address(client_ip), client_port)});

  run_pipeline(pipeline, [session_id = audio_session->session_id, udp_sink, event_bus](auto pipeline) {
    if (auto app_sink_el = gst_bin_get_by_name(GST_BIN(pipeline.get()), "wolf_udp_sink")) {
      logs::log(logs::debug, "Setting up wolf_udp_sink");
      g_assert(GST_IS_APP_SINK(app_sink_el));
      custom_sink::configure_appsink(app_sink_el, udp_sink.get());
      gst_object_unref(app_sink_el);
    }

    auto pause_handler = event_bus->register_handler<immer::box<events::PauseStreamEvent>>(
        [session_id, pipeline](const immer::box<events::PauseStreamEvent> &ev) {
          if (ev->session_id == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Pausing pipeline: {}", session_id);

            /**
             * Unfortunately here we can't just pause the pipeline,
             * when a pipeline will be resumed there are a lot of breaking changes
             * like:
             *  - Client IP:PORT
             *  - AES key and IV for encrypted payloads
             *  - Client resolution, framerate, and encoding
             *
             *  The only solution is to kill the pipeline and re-create it again
             * when a resume happens
             */

            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    auto switch_producer_handler = event_bus->register_handler<immer::box<events::SwitchStreamProducerEvents>>(
        [session_id, pipeline](const immer::box<events::SwitchStreamProducerEvents> &switch_ev) {
          if (switch_ev->session_id == session_id) {
            logs::log(logs::debug,
                      "[GSTREAMER] Switching audio producer for {} to {}",
                      session_id,
                      switch_ev->interpipe_src_id);

            auto pipe_name = fmt::format("interpipesrc_{}_audio", session_id);
            if (auto src = gst_bin_get_by_name(GST_BIN(pipeline.get()), pipe_name.c_str())) {
              /* Perform the switch */
              auto audio_interpipe = fmt::format("{}_audio", switch_ev->interpipe_src_id);
              g_object_set(src, "listen-to", audio_interpipe.c_str(), nullptr);
              gst_object_unref(src);
            } else {
              logs::log(logs::error, "[GSTREAMER] Failed to get audio interpipesrc for {}", session_id);
            }
          }
        });

    auto stop_handler = event_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [session_id, pipeline](const immer::box<events::StopStreamEvent> &ev) {
          if (ev->session_id == session_id) {
            logs::log(logs::debug, "[GSTREAMER] Stopping pipeline: {}", session_id);
            gst_element_send_event(pipeline.get(), gst_event_new_eos());
          }
        });

    return immer::array<immer::box<events::EventBusHandlers>>{std::move(pause_handler),
                                                              std::move(switch_producer_handler),
                                                              std::move(stop_handler)};
  });
}

} // namespace streaming
