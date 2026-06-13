#include <immer/vector_transient.hpp>
#include <sessions/handlers.hpp>
#include <state/config.hpp>
#include <state/sessions.hpp>

namespace wolf::core::sessions {

namespace {

virtual_display::DisplayMode to_render_mode(const moonlight::DisplayMode &display_mode) {
  return {.width = display_mode.width, .height = display_mode.height, .refreshRate = display_mode.refreshRate};
}

virtual_display::DisplayMode make_party_render_mode(const moonlight::DisplayMode &display_mode) {
  auto divider_width = (display_mode.width % 2 == 0) ? 10 : 9;
  auto tile_width = (display_mode.width - divider_width) / 2;
  return {.width = tile_width, .height = display_mode.height, .refreshRate = display_mode.refreshRate};
}

void apply_render_mode(const std::optional<events::StreamSession> &session,
                       const virtual_display::DisplayMode &render_mode) {
  if (!session) {
    return;
  }

  session->render_display_mode->store(render_mode);
  if (auto wl_state = *session->wayland_display->load()) {
    virtual_display::set_resolution(*wl_state, render_mode);
  }
}

void request_runner_restart(const std::optional<events::StreamSession> &session) {
  if (!session || !session->app->start_virtual_compositor) {
    return;
  }

  session->event_bus->fire_event(
      immer::box<events::RestartRunnerEvent>{events::RestartRunnerEvent{.session_id = session->session_id}});
}

} // namespace

std::shared_ptr<events::StreamSession> create_party_mode_secondary_session(const immer::box<state::AppState> &app_state,
                                                                           std::size_t primary_session_id,
                                                                           bool mute_secondary_audio) {
  auto sessions = app_state->running_sessions->load();
  auto primary_session = state::get_session_by_id(sessions.get(), primary_session_id);
  if (!primary_session) {
    return {};
  }

  if (state::get_party_mode_session_by_primary(app_state->party_mode_sessions->load().get(), primary_session_id)) {
    return {};
  }

  auto join_app = state::get_party_mode_join_app(app_state->config);
  if (!join_app) {
    return {};
  }

  auto local_client_id = state::gen_uuid();
  config::PairedClient local_client = {.client_cert = fmt::format("party-mode:{}", local_client_id),
                                       .app_state_folder = fmt::format("party-mode/{}", local_client_id),
                                       .settings = *primary_session->client_settings};
  auto party_render_mode = make_party_render_mode(primary_session->display_mode);

  auto secondary_session = state::create_stream_session(app_state,
                                                        *join_app.value(),
                                                        local_client,
                                                        primary_session->display_mode,
                                                        primary_session->audio_channel_count,
                                                        primary_session->aes_key,
                                                        primary_session->aes_iv);
  secondary_session->ip = primary_session->ip;
  secondary_session->render_display_mode->store(party_render_mode);
  apply_render_mode(primary_session, party_render_mode);
  request_runner_restart(primary_session);

  app_state->running_sessions->update(
      [secondary_session](const immer::vector<events::StreamSession> &current_sessions) {
        return current_sessions.push_back(*secondary_session);
      });
  app_state->party_mode_sessions->update(
      [primary_session_id, secondary_session_id = secondary_session->session_id, mute_secondary_audio](
          const immer::vector<state::PartyModeSession> &current_party_mode_sessions) {
        auto updated_sessions = state::remove_party_mode_session(current_party_mode_sessions, primary_session_id);
        updated_sessions = state::remove_party_mode_session(updated_sessions, secondary_session_id);
        return updated_sessions.push_back(state::PartyModeSession{.primary_session_id = primary_session_id,
                                                                  .secondary_session_id = secondary_session_id,
                                                                  .mute_secondary_audio = mute_secondary_audio});
      });

  app_state->event_bus->fire_event(immer::box<events::StreamSession>(*secondary_session));
  app_state->event_bus->fire_event(immer::box<events::SetPartyModeEvent>(events::SetPartyModeEvent{
      .session_id = primary_session_id,
      .enabled = true,
      .secondary_interpipe_src_id = std::to_string(secondary_session->session_id),
      .mute_secondary_audio = mute_secondary_audio,
  }));

  return secondary_session;
}

bool promote_party_mode_secondary_session(const immer::box<state::AppState> &app_state,
                                          std::size_t primary_session_id) {
  auto party_mode_sessions = app_state->party_mode_sessions->load();
  auto party_mode_session = state::get_party_mode_session_by_primary(party_mode_sessions.get(), primary_session_id);
  if (!party_mode_session) {
    return false;
  }

  auto sessions = app_state->running_sessions->load();
  auto primary_session = state::get_session_by_id(sessions.get(), party_mode_session->primary_session_id);
  auto secondary_session = state::get_session_by_id(sessions.get(), party_mode_session->secondary_session_id);
  if (!primary_session || !secondary_session) {
    return false;
  }
  apply_render_mode(secondary_session, to_render_mode(primary_session->display_mode));

  events::StreamSession promoted_session = *secondary_session;
  promoted_session.session_id = primary_session->session_id;
  promoted_session.ip = primary_session->ip;
  promoted_session.aes_key = primary_session->aes_key;
  promoted_session.aes_iv = primary_session->aes_iv;
  promoted_session.rtp_secret_payload = primary_session->rtp_secret_payload;
  promoted_session.enet_secret_payload = primary_session->enet_secret_payload;
  promoted_session.rtsp_fake_ip = primary_session->rtsp_fake_ip;
  promoted_session.video_stream_port = primary_session->video_stream_port;
  promoted_session.audio_stream_port = primary_session->audio_stream_port;
  promoted_session.control_stream_port = primary_session->control_stream_port;
  promoted_session.display_mode = primary_session->display_mode;
  promoted_session.audio_channel_count = primary_session->audio_channel_count;
  promoted_session.client_settings = primary_session->client_settings;

  app_state->running_sessions->update([&](const immer::vector<events::StreamSession> &current_sessions) {
    auto without_primary = state::remove_session(current_sessions, *primary_session);
    auto without_secondary = state::remove_session(without_primary, *secondary_session);
    return without_secondary.push_back(promoted_session);
  });
  app_state->party_mode_sessions->update([primary_session_id](const auto &current_party_mode_sessions) {
    return state::remove_party_mode_session(current_party_mode_sessions, primary_session_id);
  });
  app_state->physical_input_devices->update(
      [secondary_session_id = std::to_string(secondary_session->session_id),
       primary_session_id_str = std::to_string(primary_session->session_id)](
          const immer::vector<state::PhysicalInputDeviceAssignment> &assignments) {
        return assignments | ranges::views::transform([&](state::PhysicalInputDeviceAssignment assignment) {
                 if (assignment.owner_session_id == secondary_session_id) {
                   assignment.owner_session_id = primary_session_id_str;
                 }
                 if (assignment.routed_session_id == secondary_session_id) {
                   assignment.routed_session_id = primary_session_id_str;
                 }
                 return assignment;
               }) |
               ranges::to<immer::vector<state::PhysicalInputDeviceAssignment>>();
      });

  app_state->event_bus->fire_event(immer::box<events::SwitchStreamProducerEvents>{
      events::SwitchStreamProducerEvents{.session_id = primary_session->session_id,
                                         .interpipe_src_id = std::to_string(secondary_session->session_id)}});
  app_state->event_bus->fire_event(immer::box<events::SetPartyModeEvent>{
      events::SetPartyModeEvent{.session_id = primary_session->session_id, .enabled = false}});

  logs::log(logs::info,
            "[PARTY] Promoted secondary seat {} into primary session {}",
            secondary_session->session_id,
            primary_session->session_id);
  return true;
}

immer::vector<immer::box<events::EventBusHandlers>>
setup_party_mode_handlers(const immer::box<state::AppState> &app_state) {
  immer::vector_transient<immer::box<events::EventBusHandlers>> handlers;

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [app_state](const immer::box<events::StopStreamEvent> &stop_stream_event) {
        auto session_id = std::to_string(stop_stream_event->session_id);
        auto assigned_devices = app_state->physical_input_devices->load();
        for (const auto &assignment : *assigned_devices) {
          if (assignment.owner_session_id == session_id) {
            app_state->event_bus->fire_event(immer::box<events::UnplugDeviceEvent>{
                events::UnplugDeviceEvent{.session_id = assignment.routed_session_id,
                                          .udev_events = assignment.udev_events,
                                          .udev_hw_db_entries = assignment.udev_hw_db_entries}});
          }
        }
        app_state->physical_input_devices->update([session_id](const auto &current_assignments) {
          return state::remove_physical_input_device_assignment_by_owner(current_assignments, session_id);
        });

        auto party_mode_sessions = app_state->party_mode_sessions->load();

        if (auto primary_party_mode_session =
                state::get_party_mode_session_by_primary(party_mode_sessions.get(), stop_stream_event->session_id)) {
          app_state->party_mode_sessions->update([session_id = stop_stream_event->session_id](const auto &sessions) {
            return state::remove_party_mode_session(sessions, session_id);
          });

          app_state->event_bus->fire_event(immer::box<events::StopStreamEvent>{
              events::StopStreamEvent{.session_id = primary_party_mode_session->secondary_session_id}});
          return;
        }

        if (auto secondary_party_mode_session =
                state::get_party_mode_session_by_secondary(party_mode_sessions.get(), stop_stream_event->session_id)) {
          auto sessions = app_state->running_sessions->load();
          auto primary_session =
              state::get_session_by_id(sessions.get(), secondary_party_mode_session->primary_session_id);
          if (primary_session) {
            apply_render_mode(primary_session, to_render_mode(primary_session->display_mode));
            request_runner_restart(primary_session);
          }

          app_state->party_mode_sessions->update([session_id = stop_stream_event->session_id](const auto &sessions) {
            return state::remove_party_mode_session(sessions, session_id);
          });

          app_state->event_bus->fire_event(immer::box<events::SetPartyModeEvent>{events::SetPartyModeEvent{
              .session_id = secondary_party_mode_session->primary_session_id,
              .enabled = false,
          }});
        }
      }));

  return handlers.persistent();
}

} // namespace wolf::core::sessions
