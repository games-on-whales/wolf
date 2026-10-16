#include <core/audio.hpp>
#include <helpers/logger.hpp>
#include <memory>
#include <pulse/pulseaudio.h>
#include <vector>

namespace fmt {
template <> class formatter<wolf::core::audio::AudioMode::Speakers> {
public:
  template <typename ParseContext> constexpr auto parse(ParseContext &ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const wolf::core::audio::AudioMode::Speakers &speaker, FormatContext &ctx) const {
    std::string speaker_name;
    // Mapping taken from
    // https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/User/Modules/#module-null-sink
    switch (speaker) {
    case wolf::core::audio::AudioMode::Speakers::FRONT_LEFT:
      speaker_name = "front-left";
      break;
    case wolf::core::audio::AudioMode::Speakers::FRONT_RIGHT:
      speaker_name = "front-right";
      break;
    case wolf::core::audio::AudioMode::Speakers::FRONT_CENTER:
      speaker_name = "front-center";
      break;
    case wolf::core::audio::AudioMode::Speakers::LOW_FREQUENCY:
      speaker_name = "lfe";
      break;
    case wolf::core::audio::AudioMode::Speakers::BACK_LEFT:
      speaker_name = "rear-left";
      break;
    case wolf::core::audio::AudioMode::Speakers::BACK_RIGHT:
      speaker_name = "rear-right";
      break;
    case wolf::core::audio::AudioMode::Speakers::SIDE_LEFT:
      speaker_name = "side-left";
      break;
    case wolf::core::audio::AudioMode::Speakers::SIDE_RIGHT:
      speaker_name = "side-right";
      break;
    case wolf::core::audio::AudioMode::Speakers::MAX_SPEAKERS:
      break;
    }
    return fmt::format_to(ctx.out(), "{}", speaker_name);
  }
};
} // namespace fmt

namespace wolf::core::audio {

struct Server {
  pa_context *ctx;
  // A threaded mainloop: libpulse objects are not thread-safe, so every call into ctx from outside the mainloop
  // thread must hold this loop's lock (see MainloopLock). Callbacks already run on the mainloop thread with it held.
  pa_threaded_mainloop *loop;
  boost::promise<bool> on_ready;
  boost::future<bool> on_ready_fut;
  // Guards on_ready against being set more than once. The PA state callback
  // fires for every state change, and READY/TERMINATED/FAILED all resolve the
  // promise; without this, a connection that goes READY and later TERMINATED
  // (e.g. the PulseAudio server going away) would call set_value twice and
  // throw promise_already_satisfied. Only the single PA mainloop thread sets
  // it, so a plain bool is sufficient. Reset in disconnect() when the promise
  // is recreated.
  bool ready_set = false;
  // Operations queued while still connecting; run by the state callback once READY. Guarded by the mainloop lock.
  std::vector<std::function<void()>> pending_ops;
};

/**
 * Holds the PA mainloop lock for its lifetime. The lock is recursive, but must not be taken from the mainloop thread
 * itself (callbacks there already hold it), so this is a no-op when constructed on that thread.
 */
class MainloopLock {
public:
  explicit MainloopLock(pa_threaded_mainloop *loop) : loop(loop), locked(!pa_threaded_mainloop_in_thread(loop)) {
    if (locked) {
      pa_threaded_mainloop_lock(loop);
    }
  }
  ~MainloopLock() {
    if (locked) {
      pa_threaded_mainloop_unlock(loop);
    }
  }
  MainloopLock(const MainloopLock &) = delete;
  MainloopLock &operator=(const MainloopLock &) = delete;

private:
  pa_threaded_mainloop *loop;
  bool locked;
};

pa_context *context(const std::shared_ptr<Server> &server) {
  return server ? server->ctx : nullptr;
}

std::shared_ptr<Server> connect(std::string_view server) {
  {
    auto loop = pa_threaded_mainloop_new();
    auto ctx = pa_context_new(pa_threaded_mainloop_get_api(loop), "wolf");
    auto state = std::make_shared<Server>(Server{.ctx = ctx, .loop = loop, .on_ready = boost::promise<bool>()});
    state->on_ready_fut = state->on_ready.get_future();

    pa_context_set_state_callback(
        ctx,
        [](pa_context *ctx, void *data) {
          auto state = (Server *)data;

          switch (pa_context_get_state(ctx)) {
          case PA_CONTEXT_READY: {
            logs::log(logs::debug, "[PULSE] Pulse connection ready");
            if (!state->ready_set) {
              state->ready_set = true;
              state->on_ready.set_value(true);
            }
            auto ops = std::move(state->pending_ops);
            state->pending_ops.clear();
            for (const auto &op : ops) {
              op();
            }
            break;
          }
          case PA_CONTEXT_TERMINATED:
            logs::log(logs::debug, "[PULSE] Terminated connection");
            state->pending_ops.clear();
            if (!state->ready_set) {
              state->ready_set = true;
              state->on_ready.set_value(false);
            }
            break;
          case PA_CONTEXT_FAILED:
            logs::log(logs::debug, "[PULSE] Context failed");
            state->pending_ops.clear();
            if (!state->ready_set) {
              state->ready_set = true;
              state->on_ready.set_value(false);
            }
            break;
          case PA_CONTEXT_CONNECTING:
            logs::log(logs::debug, "[PULSE] Connecting...");
          case PA_CONTEXT_UNCONNECTED:
          case PA_CONTEXT_AUTHORIZING:
          case PA_CONTEXT_SETTING_NAME:
            break;
          }
        },
        state.get());

    // The mainloop thread isn't running yet, so no lock is needed until pa_threaded_mainloop_start()
    auto err = pa_context_connect(ctx, server.empty() ? nullptr : server.data(), PA_CONTEXT_NOFLAGS, nullptr);
    if (err) {
      logs::log(logs::warning, "[PULSE] Unable to connect, {}", pa_strerror(err));
    }

    if (pa_threaded_mainloop_start(loop) < 0) {
      logs::log(logs::warning, "[PULSE] Can't run PA mainloop");
    }

    return state;
  }
}

bool connected(const std::shared_ptr<Server> &server) {
  return server->on_ready_fut.get();
}

void queue_op(const std::shared_ptr<Server> &server, const std::function<void()> &op) {
  MainloopLock lock(server->loop);
  auto state = pa_context_get_state(server->ctx);
  if (state == PA_CONTEXT_READY) {
    op();
  } else if (PA_CONTEXT_IS_GOOD(state)) { // Still connecting, run it from the state callback once READY
    server->pending_ops.push_back(op);
  } // else: the connection failed or was closed, the op is dropped
}

/**
 * Must be called with the mainloop lock held; doesn't wait for the module to be unloaded.
 */
static void unload_sink(pa_context *ctx, uint32_t sink_idx) {
  if (sink_idx == PA_INVALID_INDEX) {
    logs::log(logs::debug, "[PULSE] Virtual sink was never created, nothing to remove");
    return;
  }
  auto operation = pa_context_unload_module(
      ctx,
      sink_idx,
      [](pa_context *c, int status, void *data) {
        logs::log(logs::debug, "[PULSE] Removed virtual sink, status: {}", status);
      },
      nullptr);
  if (operation) {
    pa_operation_unref(operation);
  }
}

std::shared_ptr<VSink> create_virtual_sink(const std::shared_ptr<Server> &server, const AudioDevice &device) {

  auto vsink = std::make_shared<VSink>();
  vsink->device = device;

  queue_op(server, [server, vsink]() {
    auto device = vsink->device;
    auto channel_spec = fmt::format("rate={} sink_name={} channels={} channel_map={}",
                                    device.mode.sample_rate,
                                    device.sink_name,
                                    device.mode.channels,
                                    fmt::join(device.mode.speakers, ","));
    auto vsink_ref = new std::shared_ptr<VSink>(vsink); // released by the callback below
    auto operation = pa_context_load_module(
        server->ctx,
        "module-null-sink",
        channel_spec.c_str(),
        [](pa_context *c, uint32_t idx, void *data) {
          // Owns a reference: delete_virtual_sink() no longer waits for this callback, so the session may already
          // have dropped its last reference to the VSink by the time PulseAudio replies.
          std::unique_ptr<std::shared_ptr<VSink>> result((std::shared_ptr<VSink> *)data);
          logs::log(logs::debug, "[PULSE] Created virtual sink: {}", idx);
          (*result)->sink_idx.set_value(idx);
          if ((*result)->unload_on_load) { // delete_virtual_sink() ran before the module was loaded
            unload_sink(c, idx);
          }
        },
        vsink_ref);
    if (operation) {
      pa_operation_unref(operation);
    } else { // The callback will never run
      logs::log(logs::warning, "[PULSE] Unable to create virtual sink: {}", pa_strerror(pa_context_errno(server->ctx)));
      delete vsink_ref;
    }
  });

  return vsink;
}

void delete_virtual_sink(const std::shared_ptr<Server> &server, const std::shared_ptr<VSink> &vsink) {
  if (vsink->deleted.exchange(true)) {
    logs::log(logs::debug, "[PULSE] Virtual sink already removed, skipping");
    return;
  }
  // Never block here waiting for the index: this runs with the mainloop lock held, and the load callback that sets
  // the index can only run on the mainloop thread once the lock is released.
  queue_op(server, [server, vsink]() {
    if (vsink->sink_idx_fut.is_ready()) {
      unload_sink(server->ctx, vsink->sink_idx_fut.get());
    } else {
      vsink->unload_on_load = true;
    }
  });
}

void disconnect(const std::shared_ptr<Server> &server) {
  MainloopLock lock(server->loop);
  server->on_ready = boost::promise<bool>(); // Creates a new promise
  server->ready_set = false;                 // ...so allow it to be set again
  pa_context_disconnect(server->ctx);
}

std::string get_server_name(const std::shared_ptr<Server> &server) {
  MainloopLock lock(server->loop);
  auto name = pa_context_get_server(server->ctx);
  return name ? name : "";
}

} // namespace wolf::core::audio
