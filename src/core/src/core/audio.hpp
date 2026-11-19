#pragma once

#include <boost/asio.hpp>
#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_FUTURE
#include <atomic>
#include <boost/thread.hpp>
#include <boost/thread/future.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct pa_context;

namespace wolf::core::audio {

typedef struct Server Server;

pa_context *context(const std::shared_ptr<Server> &server);

std::shared_ptr<Server> connect(std::string_view server = {});

constexpr auto SAMPLE_RATE = 48000;

struct AudioMode {

  enum class Speakers {
    FRONT_LEFT,
    FRONT_RIGHT,
    FRONT_CENTER,
    LOW_FREQUENCY,
    BACK_LEFT,
    BACK_RIGHT,
    SIDE_LEFT,
    SIDE_RIGHT,
    MAX_SPEAKERS,
  };

  int channels{};
  int streams{};
  int coupled_streams{};
  std::vector<Speakers> speakers;
  int bitrate{};
  int sample_rate = SAMPLE_RATE;
};

struct AudioDevice {
  std::string_view sink_name;
  AudioMode mode;
};

struct VSink {
  AudioDevice device;
  boost::promise<unsigned int> sink_idx;
  // A promise's future can only be retrieved once: read the index through this shared future instead,
  // since every runner started on the same session reaches delete_virtual_sink() when it exits.
  boost::shared_future<unsigned int> sink_idx_fut = sink_idx.get_future().share();
  // Only the first delete_virtual_sink() call unloads the module
  std::atomic<bool> deleted = false;
  // Set when delete_virtual_sink() runs before the module finished loading: the load callback unloads it instead.
  // Only touched with the PulseAudio mainloop lock held.
  bool unload_on_load = false;
};

/**
 * Will sit here and wait until the server accepts the connection or some error happens
 */
bool connected(const std::shared_ptr<Server> &server);

void queue_op(const std::shared_ptr<Server> &server, const std::function<void()> &op);

std::shared_ptr<VSink> create_virtual_sink(const std::shared_ptr<Server> &server, const AudioDevice &device);

void delete_virtual_sink(const std::shared_ptr<Server> &server, const std::shared_ptr<VSink> &vsink);

void disconnect(const std::shared_ptr<Server> &server);

std::string get_server_name(const std::shared_ptr<Server> &server);

} // namespace wolf::core::audio
