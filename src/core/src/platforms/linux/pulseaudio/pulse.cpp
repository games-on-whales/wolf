#include <core/audio.hpp>
#include <helpers/logger.hpp>
#include <memory>
#include <pulse/pulseaudio.h>

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
  pa_mainloop *loop;
  boost::promise<bool> on_ready;
  boost::future<bool> on_ready_fut;
};

std::shared_ptr<Server> connect(std::string_view server) {
  {
    auto loop = pa_mainloop_new();
    auto ctx = pa_context_new(pa_mainloop_get_api(loop), "wolf");
    auto state = std::make_shared<Server>(Server{.ctx = ctx, .loop = loop, .on_ready = boost::promise<bool>()});
    state->on_ready_fut = state->on_ready.get_future();

    pa_context_set_state_callback(
        ctx,
        [](pa_context *ctx, void *data) {
          auto state = (Server *)data;

          switch (pa_context_get_state(ctx)) {
          case PA_CONTEXT_READY:
            logs::log(logs::debug, "[PULSE] Pulse connection ready");
            state->on_ready.set_value(true);
            break;
          case PA_CONTEXT_TERMINATED:
            logs::log(logs::debug, "[PULSE] Terminated connection");
            state->on_ready.set_value(false);
            break;
          case PA_CONTEXT_FAILED:
            logs::log(logs::debug, "[PULSE] Context failed");
            state->on_ready.set_value(false);
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

    auto err = pa_context_connect(ctx, server.empty() ? nullptr : server.data(), PA_CONTEXT_NOFLAGS, nullptr);
    if (err) {
      logs::log(logs::warning, "[PULSE] Unable to connect, {}", pa_strerror(err));
    }

    std::thread([state]() {
      int retval;
      auto status = pa_mainloop_run(state->loop, &retval);
      if (status < 0) {
        logs::log(logs::warning, "[PULSE] Can't run PA mainloop");
      }
    }).detach();

    return state;
  }
}

bool connected(const std::shared_ptr<Server> &server) {
  return server->on_ready_fut.get();
}

void queue_op(const std::shared_ptr<Server> &server, const std::function<void()> &op) {
  if (pa_context_get_state(server->ctx) == PA_CONTEXT_READY) {
    op();
  } else { // Still connecting, queue an operation to be completed when connected
    server->on_ready_fut = server->on_ready_fut.then([op](boost::future<bool> res) {
      auto result = res.get();
      if (result) {
        op();
      }
      return result;
    });
  }
}

std::shared_ptr<VSink> create_virtual_sink(const std::shared_ptr<Server> &server, const AudioDevice &device) {

  auto vsink = std::make_shared<VSink>(VSink{.device = device, .sink_idx = boost::promise<unsigned int>()});

  queue_op(server, [server, vsink]() {
    auto device = vsink->device;
    auto channel_spec = fmt::format("rate={} sink_name={} channels={} channel_map={}",
                                    device.mode.sample_rate,
                                    device.sink_name,
                                    device.mode.channels,
                                    fmt::join(device.mode.speakers, ","));
    auto operation = pa_context_load_module(
        server->ctx,
        "module-null-sink",
        channel_spec.c_str(),
        [](pa_context *c, uint32_t idx, void *data) {
          auto result = (VSink *)data;
          logs::log(logs::debug, "[PULSE] Created virtual sink: {}", idx);
          result->sink_idx.set_value(idx);
        },
        vsink.get());
    pa_operation_unref(operation);
  });

  return vsink;
}

void delete_virtual_sink(const std::shared_ptr<Server> &server, const std::shared_ptr<VSink> &vsink) {
  queue_op(server, [server, vsink]() {
    auto result = std::make_shared<boost::promise<int>>();

    auto operation = pa_context_unload_module(
        server->ctx,
        vsink->sink_idx.get_future().get(),
        [](pa_context *c, int status, void *data) {
          auto result = (boost::promise<int> *)data;
          result->set_value(status);
        },
        result.get());

    auto status = result->get_future().get(); // wait for it to complete
    logs::log(logs::debug, "[PULSE] Removed virtual sink, status: {}", status);
    pa_operation_unref(operation);
  });
}

void disconnect(const std::shared_ptr<Server> &server) {
  server->on_ready = boost::promise<bool>(); // Creates a new promise
  pa_context_disconnect(server->ctx);
}

std::string get_server_name(const std::shared_ptr<Server> &server) {
  return pa_context_get_server(server->ctx);
}

} // namespace wolf::core::audio