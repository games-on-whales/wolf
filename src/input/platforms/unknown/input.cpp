#include <helpers/logger.hpp>
#include <input/input.hpp>

namespace input {

InputReady setup_handlers(std::size_t session_id,
                          const std::shared_ptr<dp::event_bus> &event_bus) {
  logs::log(logs::error, "Unable to setup input handlers for the current platform.");
  return {.devices_paths = {}, .registered_handlers = {}};
}
} // namespace input