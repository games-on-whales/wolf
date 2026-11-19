#pragma once

#include <atomic>
#include <chrono>
#include <core/audio.hpp>
#include <core/docker.hpp>
#include <events/events.hpp>
#include <future>
#include <memory>
#include <optional>
#include <state/data-structures.hpp>

namespace wolf::core::sessions {

static constexpr int DEFAULT_SESSION_TIMEOUT_MILLIS = 4000;

using namespace wolf::core;

struct AudioServer {
  std::shared_ptr<audio::Server> server;
  std::optional<docker::Container> container = {};
};

/**
 * How long a started session waits for its first RTP ping before giving up
 */
static constexpr auto DEFAULT_RTP_PING_TIMEOUT = std::chrono::seconds(30);

/**
 * Will stop the execution until an event of type RTPPingType is triggered
 * and the signature is matching the input `sess`.
 *
 * Returns the RTPPingType event, or an empty optional when the wait ended without a ping:
 * either `timeout` elapsed or a StopStreamEvent for `sess` was fired.
 * The event bus handlers registered here are always unregistered before returning.
 */
template <typename RTPPingType>
std::optional<immer::box<RTPPingType>> wait_for_ping(std::shared_ptr<events::EventBusType> ev_bus,
                                                     const auto &sess,
                                                     std::chrono::milliseconds timeout = DEFAULT_RTP_PING_TIMEOUT) {
  auto ping_promise = std::make_shared<std::promise<std::optional<immer::box<RTPPingType>>>>();
  auto ping_future = ping_promise->get_future();
  // Only the first of ping, stop and timeout gets to resolve the promise
  auto resolved = std::make_shared<std::atomic_bool>(false);

  auto ping_handler = ev_bus->register_handler<immer::box<RTPPingType>>(
      [sess, ping_promise, resolved](const immer::box<RTPPingType> &ping_ev) {
        // Check if this ping is for our session
        if (sess->rtp_secret_payload == ping_ev->payload || // Secret payload matching
            (!ping_ev->payload.has_value() && ping_ev->client_ip == sess->client_ip &&
             ping_ev->client_port == sess->port)) { // Legacy IP+port matching when no payload has been passed
          bool expected = false;
          if (resolved->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            // Resolve the promise with the ping event data
            ping_promise->set_value(ping_ev);
          }
        }
      });

  // The session can be stopped before the client ever pings
  auto stop_handler = ev_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [sess, ping_promise, resolved](const immer::box<events::StopStreamEvent> &stop_ev) {
        if (stop_ev->session_id == sess->session_id) {
          bool expected = false;
          if (resolved->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            ping_promise->set_value(std::nullopt);
          }
        }
      });

  // Wait for the promise to be fulfilled
  if (ping_future.wait_for(timeout) == std::future_status::ready) {
    return ping_future.get(); // handlers unregister when they go out of scope
  }

  // Timed out: claim the promise so that a late ping or stop can't resolve it anymore.
  // If one of them won the race in the meantime, its value is on the way.
  bool expected = false;
  if (resolved->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return std::nullopt;
  }
  return ping_future.get();
}

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
