#include "pulse_router.hpp"
#include <immer/vector_transient.hpp>

#include <core/audio.hpp>
#include <helpers/logger.hpp>

#include <pulse/pulseaudio.h>
#include <sessions/common.hpp>
#include <state/sessions.hpp>

#include <utility>

using wolf::core::sessions::VIRTUAL_SINK_PREFIX;

namespace wolf::core::audio {

immer::vector<immer::box<events::EventBusHandlers>>
setup_pulseaudio_router_handlers(const immer::box<state::AppState> &app_state,
                                 std::shared_ptr<PulseAudioRouterState> state) {
  immer::vector_transient<immer::box<events::EventBusHandlers>> handlers;

  if (!state) {
    logs::log(logs::warning, "[PULSE_ROUTER] No router state provided, cannot setup handlers");
    return handlers.persistent();
  }
  if (!app_state->event_bus) {
    logs::log(logs::warning, "[PULSE_ROUTER] No event bus provided, cannot setup handlers");
    return handlers.persistent();
  }
  if (!state->pulse_server) {
    logs::log(logs::warning, "[PULSE_ROUTER] No Pulse server provided, cannot setup handlers");
    return handlers.persistent();
  }

  // Register handlers (Wolf pattern: store registrations in `handlers`)
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::DockerContainerCreated>>(
      [state](const immer::box<events::DockerContainerCreated> &ev) {
        if (state)
          state->on_container_created(*ev);
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::DockerContainerStopped>>(
      [state](const immer::box<events::DockerContainerStopped> &ev) {
        if (state)
          state->on_container_stopped(*ev);
      }));

  // Subscribe to sink-input events and do an initial scan
  state->enable_pulse_subscribe();

  logs::log(logs::info, "[PULSE_ROUTER] Setup complete (handlers registered, PA subscribed)");
  return handlers.persistent();
}

// --- PulseAudioRouterState implementation ------------------------------------

void PulseAudioRouterState::rescan() {
  if (!pulse_server)
    return;

  audio::queue_op(pulse_server, [this]() {
    if (!pulse_server)
      return;
    auto *c = audio::context(pulse_server);
    auto op = pa_context_get_sink_input_info_list(c, &PulseAudioRouterState::pa_sink_input_info_cb, this);
    if (op)
      pa_operation_unref(op);
  });
}

void PulseAudioRouterState::on_container_created(const events::DockerContainerCreated &ev) {
  if (ev.hostname.empty() || ev.session_id.empty())
    return;

  host_to_session.update([&](auto m) { return m.set(ev.hostname, ev.session_id); });

  logs::log(logs::debug,
            "[PULSE_ROUTER] Map add host='{}' -> session='{}' (container_id='{}')",
            ev.hostname,
            ev.session_id,
            ev.container_id);

  // If sink-input already exists, route it now.
  rescan();
}

void PulseAudioRouterState::on_container_stopped(const events::DockerContainerStopped &ev) {
  if (ev.hostname.empty())
    return;

  std::optional<std::string> removed_session;

  host_to_session.update([&](auto m) {
    if (auto v = m.find(ev.hostname)) { // hostname -> session_id
      // optionally guard against hostname reuse:
      if (ev.session_id.empty() || *v == ev.session_id) {
        removed_session = *v;
        return m.erase(ev.hostname);
      }
    }
    return m;
  });

  if (removed_session && !removed_session->empty()) {
    session_to_sink_idx.update([&](auto m) { return m.erase(*removed_session); });

    logs::log(logs::debug, "[PULSE_ROUTER] Removed mappings host='{}' session='{}'", ev.hostname, *removed_session);
  }
}

// resolve sink name to sink index
void PulseAudioRouterState::pa_sink_info_cb(pa_context * /*c*/, const pa_sink_info *info, int eol, void *userdata) {
  if (eol != 0 || !info)
    return;
  auto *self = static_cast<PulseAudioRouterState *>(userdata);
  if (!self)
    return;

  if (!info->name)
    return;
  std::string_view name{info->name};

  // Prefix match
  if (!name.starts_with(VIRTUAL_SINK_PREFIX))
    return;

  // session_id is suffix after prefix
  std::string session_id{name.substr(VIRTUAL_SINK_PREFIX.size())};

  self->session_to_sink_idx.update([&](auto m) { return m.set(session_id, info->index); });

  logs::log(logs::debug,
            "[PULSE_ROUTER] Sink appeared name='{}' idx={} -> session='{}'",
            info->name,
            info->index,
            session_id);

  // route anything pending
  self->rescan();
}

void PulseAudioRouterState::enable_pulse_subscribe() {
  if (!pulse_server)
    return;

  audio::queue_op(pulse_server, [this]() {
    if (!pulse_server)
      return;
    auto *c = audio::context(pulse_server);

    pa_context_set_subscribe_callback(c, &PulseAudioRouterState::pa_subscribe_cb, this);

    auto op = pa_context_subscribe(
        c,
        static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SINK),
        nullptr,
        nullptr);
    if (op)
      pa_operation_unref(op);

    // Initial scan
    auto op2 = pa_context_get_sink_input_info_list(c, &PulseAudioRouterState::pa_sink_input_info_cb, this);
    if (op2)
      pa_operation_unref(op2);

    auto op3 = pa_context_get_sink_info_list(c, &PulseAudioRouterState::pa_sink_info_cb, this);
    if (op3)
      pa_operation_unref(op3);

    logs::log(logs::debug, "[PULSE_ROUTER] Enabled PulseAudio subscription (sink-input + sink)");
  });
}

void PulseAudioRouterState::pa_subscribe_cb(pa_context *c,
                                            pa_subscription_event_type_t t,
                                            uint32_t idx,
                                            void *userdata) {
  auto *self = static_cast<PulseAudioRouterState *>(userdata);
  if (!self)
    return;

  const auto facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  const auto type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

  if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
    if (type == PA_SUBSCRIPTION_EVENT_NEW || type == PA_SUBSCRIPTION_EVENT_CHANGE) {
      auto op = pa_context_get_sink_input_info(c, idx, &PulseAudioRouterState::pa_sink_input_info_cb, userdata);
      if (op)
        pa_operation_unref(op);
    }
    return;
  }

  if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
    if (type == PA_SUBSCRIPTION_EVENT_NEW || type == PA_SUBSCRIPTION_EVENT_CHANGE) {
      auto op = pa_context_get_sink_info_by_index(c, idx, &PulseAudioRouterState::pa_sink_info_cb, userdata);
      if (op)
        pa_operation_unref(op);
    }
    return;
  }
}

void PulseAudioRouterState::pa_sink_input_info_cb(pa_context *c,
                                                  const pa_sink_input_info *info,
                                                  int eol,
                                                  void *userdata) {
  if (eol || !info)
    return;
  auto *self = static_cast<PulseAudioRouterState *>(userdata);
  if (!self)
    return;

  self->route_sink_input_(c, info);
}

void PulseAudioRouterState::route_sink_input_(pa_context *c, const pa_sink_input_info *info) {
  if (!info || !info->proplist)
    return;

  const char *host = pa_proplist_gets(info->proplist, "application.process.host");
  if (!host || host[0] == '\0')
    return;

  // host -> session_id
  std::optional<std::string> session_id;
  {
    auto box = host_to_session.load();
    const auto &m = *box;
    if (auto v = m.find(std::string(host))) {
      session_id = *v; // immer::map find returns value ptr
    }
  }
  if (!session_id || session_id->empty())
    return;

  // session_id -> sink index
  std::optional<uint32_t> target_sink_idx;
  {
    auto box = session_to_sink_idx.load();
    const auto &m = *box;
    if (auto v = m.find(*session_id)) {
      target_sink_idx = *v;
    }
  }
  if (!target_sink_idx)
    return;

  // Already routed?
  if (info->sink == *target_sink_idx)
    return;

  // Move by index (robust)
  auto op = pa_context_move_sink_input_by_index(c, info->index, *target_sink_idx, nullptr, nullptr);
  if (op)
    pa_operation_unref(op);

  logs::log(logs::debug,
            "[PULSE_ROUTER] Move sink-input={} host='{}' -> session='{}' sink_idx={}",
            info->index,
            host,
            *session_id,
            *target_sink_idx);
}

} // namespace wolf::core::audio
