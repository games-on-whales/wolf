#include <api/api.hpp>
#include <control/input_handler.hpp>
#include <rtp/udp-ping.hpp>
#include <state/config.hpp>
#include <state/sessions.hpp>

namespace wolf::api {

void UnixSocketServer::endpoint_Events(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  // curl -N --unix-socket /tmp/wolf.sock http://localhost/api/v1/events
  state_->sockets.push_back(socket);
  send_http(socket,
            200,
            {{"Content-Type: text/event-stream"}, {"Connection: keep-alive"}, {"Cache-Control: no-cache"}},
            ""); // Inform clients this is going to be SSE
}

void UnixSocketServer::endpoint_PendingPairRequest(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto requests = std::vector<PendingPairClient>();
  for (auto [secret, pair_request] : *(state_->app_state)->pairing_atom->load()) {
    requests.push_back({.pair_secret = secret, .client_ip = pair_request->client_ip});
  }
  send_http(socket, 200, rfl::json::write(PendingPairRequestsResponse{.requests = requests}));
}

void UnixSocketServer::endpoint_Pair(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto event = rfl::json::read<PairRequest>(req.body);
  if (event) {
    if (auto pair_request = state_->app_state->pairing_atom->load()->find(event.value().pair_secret)) {
      pair_request->get().user_pin->set_value(event.value().pin.value()); // Resolve the promise
      auto res = GenericSuccessResponse{.success = true};
      send_http(socket, 200, rfl::json::write(res));
    } else {
      logs::log(logs::warning, "[API] Invalid pair secret: {}", event.value().pair_secret);
      auto res = GenericErrorResponse{.error = "Invalid pair secret"};
      send_http(socket, 500, rfl::json::write(res));
    }
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, event.error().what());
    auto res = GenericErrorResponse{.error = event.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_PairedClients(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto res = PairedClientsResponse{.success = true};
  auto clients = state_->app_state->config->paired_clients->load();
  for (const config::PairedClient &client : clients.get()) {
    res.clients.push_back(PairedClient{.client_id = std::to_string(state::get_client_id(client)),
                                       .app_state_folder = client.app_state_folder,
                                       .settings = client.settings});
  }
  send_http(socket, 200, rfl::json::write(res));
}

void UnixSocketServer::endpoint_UnpairClient(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  try {
    auto payload_result = rfl::json::read<UnpairClientRequest>(req.body);
    if (!payload_result) {
      auto res = GenericErrorResponse{.error = "Invalid request format"};
      send_http(socket, 400, rfl::json::write(res));
      return;
    }

    const auto &payload = payload_result.value(); // Unwrap the Result
    auto client = state::get_client_by_id(this->state_->app_state->config, payload.client_id.value());
    if (!client) {
      auto res = GenericErrorResponse{.error = "Client not found"};
      send_http(socket, 404, rfl::json::write(res));
      return;
    }

    state::unpair(this->state_->app_state->config, *client);

    auto res = GenericSuccessResponse{.success = true};
    send_http(socket, 200, rfl::json::write(res));
  } catch (const std::exception &e) {
    auto res = GenericErrorResponse{.error = e.what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_Apps(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto res = AppListResponse{.success = true};
  auto apps = state_->app_state->config->apps->load();
  for (const auto &app : apps.get()) {
    res.apps.push_back(rfl::Reflector<events::App>::from(app));
  }
  send_http(socket, 200, rfl::json::write(res));
}

void UnixSocketServer::endpoint_AddApp(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto app = rfl::json::read<rfl::Reflector<events::App>::ReflType>(req.body);
  if (app) {
    state_->app_state->config->apps->update([app = app.value(), this](auto &apps) {
      auto runner =
          state::get_runner(app.runner, this->state_->app_state->event_bus, this->state_->app_state->running_sessions);
      return apps.push_back(events::App{
          .base = {.title = app.title,
                   .id = app.id,
                   .support_hdr = app.support_hdr.value_or(false), // Default to false
                   .icon_png_path = app.icon_png_path},
          .video_producer_buffer_caps = app.video_producer_buffer_caps.value_or("video/x-raw(memory:DMABuf)"), // Default for NVIDIA GPU zero-copy
          .h264_gst_pipeline = app.h264_gst_pipeline.value_or("interpipesrc listen-to={session_id}_video is-live=true stream-sync=restart-ts max-bytes=0 max-buffers=1 leaky-type=downstream ! video/x-raw, width={width}, height={height}, framerate={fps}/1 ! nvh264enc preset=low-latency-hq zerolatency=true gop-size=0 rc-mode=cbr-ld-hq bitrate={bitrate} aud=false ! h264parse ! video/x-h264, profile=main, stream-format=byte-stream ! rtpmoonlightpay_video name=moonlight_pay payload_size={payload_size} fec_percentage={fec_percentage} min_required_fec_packets={min_required_fec_packets} ! appsink sync=false name=wolf_udp_sink"),
          .hevc_gst_pipeline = app.hevc_gst_pipeline.value_or(""),  // Empty when HEVC not available (matches TOML)
          .av1_gst_pipeline = app.av1_gst_pipeline.value_or(""),    // Empty when AV1 not available (matches TOML)
          .render_node = app.render_node.value_or("/dev/dri/renderD128"),  // Use system default
          .opus_gst_pipeline = app.opus_gst_pipeline.value_or("interpipesrc listen-to={session_id}_audio is-live=true stream-sync=restart-ts max-bytes=0 max-buffers=3 block=false ! queue max-size-buffers=3 leaky=downstream ! audiorate ! audioconvert ! opusenc bitrate={bitrate} bitrate-type=cbr frame-size={packet_duration} bandwidth=fullband audio-type=restricted-lowdelay max-payload-size=1400 ! rtpmoonlightpay_audio name=moonlight_pay packet_duration={packet_duration} encrypt={encrypt} aes_key=\"{aes_key}\" aes_iv=\"{aes_iv}\" ! appsink name=wolf_udp_sink"),
          .start_virtual_compositor = app.start_virtual_compositor.value_or(true), // Default to true
          .start_audio_server = app.start_audio_server.value_or(true), // Default to true
          .runner = runner,
      });
    });

    logs::log(logs::info, "[API] App addition completed, checking auto-start configuration");
    // Auto-start container if enabled
    logs::log(logs::debug, "[API] Auto-start containers setting: {}", state_->app_state->config->auto_start_containers);
    if (state_->app_state->config->auto_start_containers) {
      logs::log(logs::info, "[API] Auto-start is enabled, looking for app: {}", app.value().id);
      auto new_app = state::get_app_by_id(this->state_->app_state->config, app.value().id);
      if (new_app) {
        logs::log(logs::info, "[API] Auto-starting container for app: {}", app.value().id);
        try {
          // Create a virtual background client for auto-started containers
          auto background_client = wolf::config::PairedClient{
              .client_cert = "-----BEGIN CERTIFICATE-----\nVIRTUAL_BACKGROUND_CLIENT_FOR_AUTO_START\n-----END CERTIFICATE-----",
              .app_state_folder = fmt::format("auto_start_{}", app.value().id),
              .settings = wolf::config::ClientSettings{
                  .run_uid = 1000,
                  .run_gid = 1000,
                  .controllers_override = {},
                  .mouse_acceleration = 1.0,
                  .v_scroll_acceleration = 1.0,
                  .h_scroll_acceleration = 1.0,
              }};

          // Create a minimal stream session for the auto-started container
          // Use iPad resolution (2360x1640@120fps) as specified by user
          auto display_mode = moonlight::DisplayMode{
              .width = 2360,
              .height = 1640,
              .refreshRate = 120,
              .hevc_supported = state_->app_state->config->support_hevc,
              .av1_supported = state_->app_state->config->support_av1};

          auto background_session = state::create_stream_session(
              state_->app_state,
              *new_app.value(),
              background_client,
              display_mode,
              2, // Stereo audio
              "9d804e47a6aa6624b7d4b502b32cc522", // 32-char hex for AES-128
              "0123456789abcdef" // 16-char hex for IV
          );

          // Add the session to running sessions so it can be found for reuse
          state_->app_state->running_sessions->update(
              [background_session](const immer::vector<events::StreamSession> &ses_v) {
                return ses_v.push_back(*background_session);
              });

          auto runner = new_app.value()->runner;
          state_->app_state->event_bus->fire_event(immer::box<events::StartRunner>(
              events::StartRunner{.stop_stream_when_over = false,
                                  .runner = runner,
                                  .stream_session = background_session}));

          // Also start video and audio sessions to set up Wayland display infrastructure
          auto video_session = events::VideoSession{
              .session_id = background_session->session_id,
              .width = background_session->display_mode.width,
              .height = background_session->display_mode.height,
              .fps = background_session->display_mode.refreshRate,
              .slices_per_frame = 8,
              .bitrate = 20000000, // 20 Mbps for background sessions
              .colour_range = "full",
              .colour_space = "bt709",
              .encoder_type = "HEVC"
          };
          state_->app_state->event_bus->fire_event(immer::box<events::VideoSession>(video_session));

          auto audio_session = events::AudioSession{
              .session_id = background_session->session_id,
              .bitrate = 96000, // 96 kbps for stereo audio
              .channel_count = 2,
              .packet_duration = 20000, // 20ms packets
              .encrypt_data = false // No encryption for background sessions
          };
          state_->app_state->event_bus->fire_event(immer::box<events::AudioSession>(audio_session));

          logs::log(logs::info, "[API] Auto-started container with background session and video/audio: {}", background_session->session_id);
        } catch (const std::exception &e) {
          logs::log(logs::warning, "[API] Failed to auto-start container for app {}: {}", app.value().id, e.what());
        }
      }
    }

    auto res = GenericSuccessResponse{.success = true};
    send_http(socket, 200, rfl::json::write(res));
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, app.error().what());
    auto res = GenericErrorResponse{.error = app.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_RemoveApp(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto app = rfl::json::read<AppDeleteRequest>(req.body);
  if (app) {
    state_->app_state->config->apps->update([app = app.value()](auto &apps) {
      return apps |                                                                                             //
             ranges::views::filter([&app](const immer::box<events::App> &a) { return a->base.id != app.id; }) | //
             ranges::to<immer::vector<immer::box<events::App>>>();
    });
    auto res = GenericSuccessResponse{.success = true};
    send_http(socket, 200, rfl::json::write(res));
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, app.error().what());
    auto res = GenericErrorResponse{.error = app.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_StreamSessions(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto res = StreamSessionListResponse{.success = true};
  auto sessions = state_->app_state->running_sessions->load();
  for (const auto &session : sessions.get()) {
    res.sessions.push_back(rfl::Reflector<events::StreamSession>::from(session));
  }
  send_http(socket, 200, rfl::json::write(res));
}

void UnixSocketServer::endpoint_StreamSessionAdd(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto session = rfl::json::read<rfl::Reflector<events::StreamSession>::ReflType>(req.body);
  if (session) {
    auto ss = session.value();
    auto app = state::get_app_by_id(this->state_->app_state->config, ss.app_id);
    if (!app) {
      logs::log(logs::warning, "[API] Invalid app_id: {}", ss.app_id);
      auto res = GenericErrorResponse{.error = "Invalid app_id"};
      send_http(socket, 500, rfl::json::write(res));
      return;
    }

    auto client = state::get_client_by_id(this->state_->app_state->config, ss.client_id);
    if (!client) {
      logs::log(logs::warning, "[API] Invalid client_id: {}", ss.client_id);
      auto res = GenericErrorResponse{.error = "Invalid client_id"};
      send_http(socket, 500, rfl::json::write(res));
      return;
    }

    // Check for existing session if reuse_existing_sessions is enabled
    if (state_->app_state->config->reuse_existing_sessions) {
      auto sessions = state_->app_state->running_sessions->load();
      if (auto existing_session = state::get_session_by_app_id(sessions.get(), ss.app_id)) {
        logs::log(logs::info, "[API] Reusing existing session for app_id: {}, session_id: {}",
                 ss.app_id, existing_session->session_id);

        // Update session resolution to match the newest client's request
        auto updated_session = *existing_session;
        updated_session.display_mode.width = ss.video_width;
        updated_session.display_mode.height = ss.video_height;
        updated_session.display_mode.refreshRate = ss.video_refresh_rate;
        updated_session.ip = ss.client_ip;
        updated_session.rtsp_fake_ip = ss.rtsp_fake_ip;

        // Update the session in the state
        state_->app_state->running_sessions->update(
            [&updated_session](const immer::vector<events::StreamSession> &ses_v) {
              return state::remove_session(ses_v, *existing_session).push_back(updated_session);
            });

        logs::log(logs::info, "[API] Updated session resolution to {}x{}@{}fps for session_id: {}",
                 ss.video_width, ss.video_height, ss.video_refresh_rate, existing_session->session_id);

        // Return existing session ID
        auto res = StreamSessionCreated{.success = true, .session_id = std::to_string(existing_session->session_id)};
        send_http(socket, 200, rfl::json::write(res));
        return;
      }
    }

    auto new_session = state::create_stream_session( //
        state_->app_state,
        app.value(),
        client.value(),
        moonlight::DisplayMode{.width = ss.video_width,
                               .height = ss.video_height,
                               .refreshRate = ss.video_refresh_rate,
                               .hevc_supported = state_->app_state->config->support_hevc,
                               .av1_supported = state_->app_state->config->support_av1},
        ss.audio_channel_count,
        ss.aes_key,
        ss.aes_iv);
    new_session->ip = ss.client_ip;
    new_session->rtsp_fake_ip = ss.rtsp_fake_ip;

    state_->app_state->running_sessions->update(
        [new_session](const immer::vector<events::StreamSession> &ses_v) { return ses_v.push_back(*new_session); });
    state_->app_state->event_bus->fire_event(immer::box<events::StreamSession>(*new_session));

    auto res = StreamSessionCreated{.success = true, .session_id = std::to_string(new_session->session_id)};
    send_http(socket, 200, rfl::json::write(res));
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, session.error().what());
    auto res = GenericErrorResponse{.error = session.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_StreamSessionStart(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto start_req = rfl::json::read<StreamSessionStartRequest>(req.body);
  if (start_req) {
    auto sessions = state_->app_state->running_sessions->load();
    auto session_id = std::stoul(start_req.value().session_id);
    if (auto session = state::get_session_by_id(sessions.get(), session_id)) {
      auto video_session = start_req.value().video_session;
      video_session.session_id = session_id; // Can't be JSON encoded
      state_->app_state->event_bus->fire_event(immer::box<events::VideoSession>(video_session));

      auto audio_session = start_req.value().audio_session;
      audio_session.session_id = session_id; // Can't be JSON encoded
      state_->app_state->event_bus->fire_event(immer::box<events::AudioSession>(audio_session));

      auto res = GenericSuccessResponse{.success = true};
      send_http(socket, 200, rfl::json::write(res));
    } else {
      logs::log(logs::warning, "[API] Invalid session_id: {}", session.value().session_id);
      auto res = GenericErrorResponse{.error = "Invalid session_id"};
      send_http(socket, 500, rfl::json::write(res));
    }
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, start_req.error().what());
    auto res = GenericErrorResponse{.error = start_req.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_StreamSessionPause(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto session = rfl::json::read<StreamSessionPauseRequest>(req.body);
  if (session) {
    auto sessions = state_->app_state->running_sessions->load();
    auto session_id = std::stoul(session.value().session_id);
    if (state::get_session_by_id(sessions.get(), session_id)) {
      this->state_->app_state->event_bus->fire_event(
          immer::box<events::PauseStreamEvent>(events::PauseStreamEvent{.session_id = session_id}));
      auto res = GenericSuccessResponse{.success = true};
      send_http(socket, 200, rfl::json::write(res));
    } else {
      logs::log(logs::warning, "[API] Invalid session_id: {}", session.value().session_id);
      auto res = GenericErrorResponse{.error = "Invalid session_id"};
      send_http(socket, 500, rfl::json::write(res));
    }
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, session.error().what());
    auto res = GenericErrorResponse{.error = session.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_StreamSessionStop(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto session = rfl::json::read<StreamSessionStopRequest>(req.body);
  if (session) {
    auto sessions = state_->app_state->running_sessions->load();
    auto session_id = std::stoul(session.value().session_id);
    if (auto found_session = state::get_session_by_id(sessions.get(), session_id)) {

      // Check if this is an auto-started persistent session by examining the virtual client
      auto client = state::get_client_by_id(this->state_->app_state->config, session_id);
      bool is_auto_started_session = false;
      if (client && client->client_cert.find("VIRTUAL_BACKGROUND_CLIENT_FOR_AUTO_START") != std::string::npos) {
        is_auto_started_session = true;
      }

      if (is_auto_started_session) {
        // For auto-started persistent sessions, stop is a no-op to preserve running AI agents
        logs::log(logs::info, "[API] Stop request ignored for auto-started persistent session: {}", session_id);
        auto res = GenericSuccessResponse{.success = true};
        send_http(socket, 200, rfl::json::write(res));
        return;
      } else {
        // Normal stop behavior for user-initiated sessions
        this->state_->app_state->event_bus->fire_event(
            immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = session_id}));
        auto res = GenericSuccessResponse{.success = true};
        send_http(socket, 200, rfl::json::write(res));
        return;
      }
    } else {
      logs::log(logs::warning, "[API] Invalid session_id: {}", session.value().session_id);
      auto res = GenericErrorResponse{.error = "Invalid session_id"};
      send_http(socket, 500, rfl::json::write(res));
    }
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, session.error().what());
    auto res = GenericErrorResponse{.error = session.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_StreamSessionHandleInput(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto input_request = rfl::json::read<StreamSessionHandleInputRequest>(req.body);
  if (input_request) {
    auto sessions = state_->app_state->running_sessions->load();
    auto session_id = std::stoul(input_request.value().session_id);
    if (auto session = state::get_session_by_id(sessions.get(), session_id)) {
      auto hex_pkt = input_request.value().input_packet_hex.get();
      auto pkt_parsed = crypto::hex_to_str(hex_pkt);
      control::INPUT_PKT *input_pkt = reinterpret_cast<control::INPUT_PKT *>(pkt_parsed.data());
      control::handle_input(session.value(), {}, input_pkt);

      send_http(socket, 200, rfl::json::write(GenericSuccessResponse{.success = true}));
    } else {
      logs::log(logs::warning, "[API] Invalid session_id: {}", input_request.value().session_id);
      send_http(socket, 500, rfl::json::write(GenericErrorResponse{.error = "Invalid session_id"}));
    }
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, input_request.error().what());
    send_http(socket, 500, rfl::json::write(GenericErrorResponse{.error = input_request.error().what()}));
  }
}

void UnixSocketServer::endpoint_RunnerStart(const wolf::api::HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto event = rfl::json::read<RunnerStartRequest>(req.body);
  if (event) {
    auto session = state::get_session_by_id(this->state_->app_state->running_sessions->load(),
                                            std::stoul(event.value().session_id));
    if (!session) {
      logs::log(logs::warning, "[API] Invalid session_id: {}", event.value().session_id);
      auto res = GenericErrorResponse{.error = "Invalid session_id"};
      send_http(socket, 500, rfl::json::write(res));
      return;
    }

    auto runner = state::get_runner(event.value().runner,
                                    this->state_->app_state->event_bus,
                                    this->state_->app_state->running_sessions);
    state_->app_state->event_bus->fire_event(immer::box<events::StartRunner>(
        events::StartRunner{.stop_stream_when_over = event.value().stop_stream_when_over,
                            .runner = runner,
                            .stream_session = std::make_shared<events::StreamSession>(*session)}));
  } else {
    logs::log(logs::warning, "[API] Invalid event: {} - {}", req.body, event.error().what());
    auto res = GenericErrorResponse{.error = event.error().what()};
    send_http(socket, 500, rfl::json::write(res));
  }
}

void UnixSocketServer::endpoint_UpdateClientSettings(const HTTPRequest &req, std::shared_ptr<UnixSocket> socket) {
  auto payload_result = rfl::json::read<UpdateClientSettingsRequest>(req.body);
  if (!payload_result) {
    auto res = GenericErrorResponse{.error = "Invalid request format"};
    send_http(socket, 400, rfl::json::write(res));
    return;
  }

  const auto &payload = payload_result.value();
  auto current_client = state::get_client_by_id(this->state_->app_state->config, payload.client_id.value());
  if (!current_client) {
    auto res = GenericErrorResponse{.error = "Client not found"};
    send_http(socket, 404, rfl::json::write(res));
    return;
  }

  // Edit only the settings that are being passed in the payload
  auto current_settings = current_client->settings;
  auto new_settings = payload.settings.get().value_or(PartialClientSettings{});
  auto merged_client = config::PairedClient{
      .client_cert = current_client->client_cert, // Immutable, changing this would mean a new client
      .app_state_folder = payload.app_state_folder.get().value_or(current_client->app_state_folder),
      .settings = config::ClientSettings{
          .run_uid = new_settings.run_gid.value_or(current_settings.run_uid),
          .run_gid = new_settings.run_gid.value_or(current_settings.run_gid),
          .controllers_override = new_settings.controllers_override.value_or(current_settings.controllers_override),
          .mouse_acceleration = new_settings.mouse_acceleration.value_or(current_settings.mouse_acceleration),
          .v_scroll_acceleration = new_settings.v_scroll_acceleration.value_or(current_settings.v_scroll_acceleration),
          .h_scroll_acceleration = new_settings.h_scroll_acceleration.value_or(current_settings.h_scroll_acceleration),
      }};

  update_client_settings(this->state_->app_state->config, std::stoull(payload.client_id.value()), merged_client);

  auto res = GenericSuccessResponse{.success = true};
  send_http(socket, 200, rfl::json::write(res));
}

} // namespace wolf::api