#pragma once

#include <core/audio.hpp>
#include <core/docker.hpp>
#include <events/events.hpp>
#include <functional>
#include <state/data-structures.hpp>

namespace wolf::core::sessions {

static constexpr int DEFAULT_SESSION_TIMEOUT_MILLIS = 4000;

using namespace wolf::core;

struct AudioServer {
  std::shared_ptr<audio::Server> server;
  std::optional<docker::Container> container = {};
};

/**
 * The video/audio pipelines of a stream. Each call blocks until its pipeline has ended (on pause, stop, or when a
 * resumed stream supersedes it).
 */
struct StreamPipelines {
  std::function<void(const immer::box<events::VideoSession> &, const immer::box<events::RTPVideoPingEvent> &)> video;
  std::function<void(const immer::box<events::AudioSession> &, const immer::box<events::RTPAudioPingEvent> &)> audio;
};

/**
 * Starts a stream's pipelines on the client's first ping and keeps its StreamSession::stream_state in step with
 * them and with pauses. Registered by setup_moonlight_handlers; separate so tests can run it with fake pipelines.
 */
immer::vector<immer::box<events::EventBusHandlers>>
setup_stream_pipeline_handlers(const std::shared_ptr<events::EventBusType> &ev_bus,
                               const state::SessionsAtoms &running_sessions,
                               const StreamPipelines &pipelines);

immer::vector<immer::box<events::EventBusHandlers>>
setup_moonlight_handlers(const immer::box<state::AppState> &app_state,
                         const std::string &runtime_dir,
                         const std::optional<AudioServer> &audio_server);

immer::vector<immer::box<events::EventBusHandlers>>
setup_lobbies_handlers(const immer::box<state::AppState> &app_state,
                       const std::string &runtime_dir,
                       const std::optional<AudioServer> &audio_server);

struct RunnerArgs {
  const std::string &session_id;

  const events::VideoSettings video_settings;
  const virtual_display::wl_state_ptr wayland_display;

  const std::optional<AudioServer> &audio_server;
  const std::shared_ptr<audio::VSink> audio_sink;

  const immer::box<state::Host> host;
  const std::string &app_local_state_folder;
  const std::string &app_host_state_folder;
  const std::string &xdg_runtime_dir;

  immer::box<config::ClientSettings> client_settings;
};

/**
 * Setup and starts the runner. The current thread will be blocked until the
 * runner is stopped.
 */
void start_runner(std::shared_ptr<events::Runner> runner,
                  std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
                  immer::box<RunnerArgs> args);

} // namespace wolf::core::sessions