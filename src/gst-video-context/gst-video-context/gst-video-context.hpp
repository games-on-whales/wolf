#pragma once

#include <gst/gst.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <optional>

namespace gst_video_context {

/**
 * Dynamically links and load up the required libraries; needs to be called once.
 */
bool init();

/** Return the CUDA ordinal corresponding to a DRM render node, when it is an NVIDIA GPU. */
std::optional<int> getCudaDeviceFromDri(const std::string &device_path);

struct GstVideoContext;
using gst_context_ptr = std::shared_ptr<GstVideoContext>;

class GstVideoContextProvider {
public:
  gst_context_ptr get_or_create(const std::string &device_path);

private:
  std::mutex mutex_;
  std::map<std::string, gst_context_ptr> contexts_;
};

/**
 * Given a GstMessage will automatically set the context if it's a GST_MESSAGE_NEED_CONTEXT
 * and we support the required context type.
 *
 * Returns a smart pointer to the created context, it's up to the caller to store it
 * properly for the duration of the pipeline. Returns nullptr if we haven't created any context.
 */
gst_context_ptr need_context_for_device(const std::string &device_path, GstMessage *msg);

bool set_context(gst_context_ptr context, GstMessage *msg);

/** Set the selected GPU context on a pipeline before it changes state. */
bool set_context(gst_context_ptr context, GstElement *element);

} // namespace gst_video_context
