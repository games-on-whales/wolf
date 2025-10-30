#include <api/api.hpp>
#include <control/input_handler.hpp>
#include <rtp/udp-ping.hpp>
#include <state/config.hpp>
#include <state/sessions.hpp>
#include <random>
#include <fmt/format.h>

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
    // Check if app with same ID already exists (make AddApp idempotent)
    bool app_already_exists = false;
    auto existing_apps = state_->app_state->config->apps->load();
    for (const auto &existing_app : existing_apps.get()) {
      if (existing_app->base.id == app.value().id) {
        app_already_exists = true;
        logs::log(logs::debug, "[API] App with ID '{}' already exists, skipping addition", app.value().id);
        break;
      }
    }

    if (!app_already_exists) {
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
          .default_display_width = app.default_display_width,
          .default_display_height = app.default_display_height,
          .default_display_fps = app.default_display_fps,
          .runner = runner,
      });
    });

      logs::log(logs::info, "[API] App addition completed, checking auto-start configuration");
      // Auto-start container if enabled
      logs::log(logs::debug, "[API] Auto-persistent sessions setting: {}", state_->app_state->config->auto_persistent_sessions);
      if (state_->app_state->config->auto_persistent_sessions) {
      logs::log(logs::info, "[API] Auto-start is enabled, looking for app: {}", app.value().id);
      auto new_app = state::get_app_by_id(this->state_->app_state->config, app.value().id);
      if (new_app) {
        logs::log(logs::info, "[API] Auto-starting container for app: {}", app.value().id);
        try {
          // Use default client settings for background sessions (no virtual client needed)
          auto background_client_settings = wolf::config::ClientSettings{
              .run_uid = 1000,
              .run_gid = 1000,
              .controllers_override = {},
              .mouse_acceleration = 1.0,
              .v_scroll_acceleration = 1.0,
              .h_scroll_acceleration = 1.0,
          };

          // Use app's default display configuration if available, otherwise fallback to iPad resolution
          auto display_mode = moonlight::DisplayMode{
              .width = new_app.value()->default_display_width.value_or(2360),  // Use app default or fallback to iPad width
              .height = new_app.value()->default_display_height.value_or(1640), // Use app default or fallback to iPad height
              .refreshRate = new_app.value()->default_display_fps.value_or(120), // Use app default or fallback to 120fps
              .hevc_supported = state_->app_state->config->support_hevc,
              .av1_supported = state_->app_state->config->support_av1};

          // Generate session parameters like the normal create_stream_session function
          std::random_device rd;
          std::mt19937 generator(rd());
          std::uniform_int_distribution<> chars(33, 126);
          std::array<char, 16> rtp_secret_payload;
          for (auto &c : rtp_secret_payload) {
            c = static_cast<char>(chars(generator));
          }
          std::uniform_int_distribution<u_int32_t> uints(0, UINT32_MAX);
          std::uniform_int_distribution<> ints(0, 255);
          auto rtsp_fake_ip = fmt::format("{}.{}.{}.{}", ints(generator), ints(generator), ints(generator), ints(generator));

          // Create directories for background session
          auto app_local_folder = std::filesystem::path(state_->app_state->host->local_base_state_folder) / "helix_background_sessions" / new_app.value()->base.title;
          auto app_host_folder = std::filesystem::path(state_->app_state->host->host_base_state_folder) / "helix_background_sessions" / new_app.value()->base.title;
          std::filesystem::create_directories(app_local_folder);
          std::filesystem::create_directories(app_host_folder);

          // Create background session directly without virtual client
          auto background_session = std::make_shared<events::StreamSession>(events::StreamSession{
              .display_mode = display_mode,
              .audio_channel_count = 2, // Stereo
              .event_bus = state_->app_state->event_bus,
              .client_settings = immer::box<wolf::config::ClientSettings>(background_client_settings),
              .app = std::make_shared<events::App>(*new_app.value()),
              .app_local_state_folder = app_local_folder.string(),
              .app_host_state_folder = app_host_folder.string(),
              .aes_key = "9d804e47a6aa6624b7d4b502b32cc522", // 32-char hex for AES-128
              .aes_iv = "0123456789abcdef", // 16-char hex for IV
              .rtp_secret_payload = rtp_secret_payload,
              .enet_secret_payload = uints(generator),
              .rtsp_fake_ip = rtsp_fake_ip,
              .session_id = state_->app_state->config->auto_persistent_sessions ?
                          std::hash<std::string>{}(new_app.value()->base.id) :  // Persistent session: use app ID hash
                          uints(generator), // Individual sessions: use random ID
              .ip = "127.0.0.1", // Local background session
              .video_stream_port = static_cast<unsigned short>(state::get_port(state::VIDEO_PING_PORT)),
              .audio_stream_port = static_cast<unsigned short>(state::get_port(state::AUDIO_PING_PORT)),
              .control_stream_port = static_cast<unsigned short>(state::get_port(state::CONTROL_PORT)),
          });

          // Follow the exact same pattern as normal Moonlight sessions:
          // 1. Fire StreamSession event (triggers automatic setup of video/audio/Wayland)
          // 2. Add to running sessions list
          state_->app_state->event_bus->fire_event(immer::box<events::StreamSession>(*background_session));
          state_->app_state->running_sessions->update(
              [background_session](const immer::vector<events::StreamSession> &ses_v) {
                return ses_v.push_back(*background_session);
              });

          logs::log(logs::info, "[API] Auto-started container with background session and video/audio: {}", background_session->session_id);
        } catch (const std::exception &e) {
          logs::log(logs::warning, "[API] Failed to auto-start container for app {}: {}", app.value().id, e.what());
        }
      }
      }
    } // End of if (!app_already_exists)

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

    // TODO: SESSION SHARING ARCHITECTURE ISSUES
    // CRITICAL: True session sharing is NOT implemented - only session replacement works:
    // 1. OVERWRITES session metadata (resolution, IP, RTSP routing) - breaks previous clients
    // 2. NO video pipeline restart - new client gets old resolution, not what they requested
    // 3. NO multi-client support - each "reuse" kicks out the previous client
    // 4. Wolf video pipelines have resolution HARDCODED at startup - cannot change dynamically
    // 5. Restarting video pipeline = killing compositor = losing app state (Zed, Hyprland, etc.)
    //
    // CURRENT BEHAVIOR: "Session sharing" = session hijacking by newest client
    // NEW CLIENT EXPERIENCE: Connects but gets wrong resolution (background session's resolution)
    // PREVIOUS CLIENT EXPERIENCE: Gets disconnected/broken when new client connects
    //
    // REALITY: This enables "single client connecting to persistent background container"
    // NOT TRUE SHARING: Multiple simultaneous clients is not architecturally possible

    // Check for existing session if auto_persistent_sessions is enabled
    if (state_->app_state->config->auto_persistent_sessions) {
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
            [&updated_session, existing_session](const immer::vector<events::StreamSession> &ses_v) {
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

      // Check if this is a persistent background session
      // If auto_persistent_sessions is enabled, all sessions should be persistent
      bool is_background_session = state_->app_state->config->auto_persistent_sessions;

      if (is_background_session) {
        // For persistent background sessions, stop is a no-op to preserve running containers
        logs::log(logs::info, "[API] Stop request ignored for persistent background session: {}", session_id);
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