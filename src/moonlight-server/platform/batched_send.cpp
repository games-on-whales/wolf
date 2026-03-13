#include <cstring>
#include <fcntl.h>
#include <helpers/logger.hpp>
#include <platform/batched_send.hpp>

namespace wolf::platform {

buffer_descriptor_t batched_send_info_t::buffer_for_payload_offset(ptrdiff_t offset) const {
  for (const auto &desc : payload_buffers) {
    if (offset < (ptrdiff_t)desc.size) {
      return {
          desc.buffer + offset,
          desc.size - offset,
      };
    } else {
      offset -= desc.size;
    }
  }
  return {};
}

} // namespace wolf::platform
