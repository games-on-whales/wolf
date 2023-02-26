#include <audio/audio.hpp>
#include <helpers/logger.hpp>

namespace audio {

struct Server {};

std::shared_ptr<Server> connect(boost::asio::thread_pool &t_pool) {
  logs::log(logs::warning, "Virtual audio unsupported for the current platform.");
  return std::make_shared<Server>();
}

std::shared_ptr<VSink> create_virtual_sink(const std::shared_ptr<Server> &server, const AudioDevice &device) {
  return std::make_shared<VSink>();
}

void delete_virtual_sink(const std::shared_ptr<Server> &server, const std::shared_ptr<VSink> &vsink) {}

} // namespace audio