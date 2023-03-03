#pragma once

#include <boost/asio.hpp>
#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_FUTURE
#include <boost/thread.hpp>
#include <boost/thread/future.hpp>
#include <functional>
#include <memory>
#include <string>

namespace audio {

typedef struct Server Server;

std::shared_ptr<Server> connect(boost::asio::thread_pool &t_pool, std::string_view server = {});

struct AudioDevice {
  std::string_view sink_name;
  int n_channels;
  int bitrate = 48000;
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

} // namespace audio