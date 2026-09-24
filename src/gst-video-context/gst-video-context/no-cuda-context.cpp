#include "gst-video-context.hpp"

namespace gst_video_context {

gst_context_ptr GstVideoContextProvider::get_or_create(const std::string &) {
  return nullptr;
}

struct GstVideoContext {};

bool init() {
  return true;
}

std::optional<int> getCudaDeviceFromDri(const std::string &) {
  return std::nullopt;
}

gst_context_ptr need_context_for_device(const std::string &device_path, GstMessage *msg) {
  return nullptr;
}

bool set_context(gst_context_ptr context, GstMessage *msg) {
  return false;
}

bool set_context(gst_context_ptr context, GstElement *element) {
  return false;
}

} // namespace gst_video_context
