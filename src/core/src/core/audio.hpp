#pragma once

#include <boost/asio.hpp>
#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_FUTURE
#include <boost/thread.hpp>
#include <boost/thread/future.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wolf::core::audio {

typedef struct Server Server;

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
};

/**
 * Will sit here and wait until the server accepts the connection or some error happens
 */
bool connected(const std::shared_ptr<Server> &server);

std::shared_ptr<VSink> create_virtual_sink(const std::shared_ptr<Server> &server, const AudioDevice &device);

void delete_virtual_sink(const std::shared_ptr<Server> &server, const std::shared_ptr<VSink> &vsink);

void disconnect(const std::shared_ptr<Server> &server);

std::string get_server_name(const std::shared_ptr<Server> &server);

} // namespace wolf::core::audio