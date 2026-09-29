#include "gst-video-context.hpp"

namespace gst_video_context {

struct GstVideoContext {};

bool init() {
  return true;
}

gst_context_ptr need_context_for_device(const std::string &device_path, GstMessage *msg) {
  return nullptr;
}

bool is_context_valid(const gst_context_ptr &context) {
  return false;
}

bool set_context(gst_context_ptr context, GstMessage *msg) {
  return false;
}

} // namespace gst_video_context
