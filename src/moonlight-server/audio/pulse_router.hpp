#pragma once

#include <events/events.hpp>
#include <immer/box.hpp>
#include <immer/vector.hpp>

#include <mutex>
#include <optional>
#include <pulse/pulseaudio.h>
#include <state/sessions.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

struct pa_context;
struct pa_sink_input_info;

namespace wolf::core::audio {

struct Server;

/**
 * PulseAudio routing state.
 * Kept alive by the caller together with the returned handler registrations.
 */
struct PulseAudioRouterState {
  std::shared_ptr<audio::Server> pulse_server;

  // hostname -> session_id
  immer::atom<immer::map<std::string, std::string>> host_to_session{immer::map<std::string, std::string>{}};

  // session_id -> sink_index (from VirtualAudioSinkCreated)
  immer::atom<immer::map<std::string, uint32_t>> session_to_sink_idx{immer::map<std::string, uint32_t>{}};

  // Pulse subscribe / callbacks
  void enable_pulse_subscribe();
  void rescan();

  static void pa_subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata);
  static void pa_sink_input_info_cb(pa_context *c, const pa_sink_input_info *info, int eol, void *userdata);
  static void pa_sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata);

  void on_container_created(const events::DockerContainerCreated &ev);
  void on_container_stopped(const events::DockerContainerStopped &ev);

  void route_sink_input_(pa_context *c, const pa_sink_input_info *info);
};

/**
 * Registers event handlers needed for routing, and enables PulseAudio subscribe.
 *
 * IMPORTANT:
 * - The returned handlers must be kept alive, otherwise handlers unregister.
 * - `state` must be kept alive as long as PulseAudio callbacks can fire.
 */
immer::vector<immer::box<events::EventBusHandlers>>
setup_pulseaudio_router_handlers(const immer::box<state::AppState> &app_state,
                                 std::shared_ptr<PulseAudioRouterState> state);

} // namespace wolf::core::audio
