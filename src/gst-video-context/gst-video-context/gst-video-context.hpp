#pragma once

#include <gst/gst.h>
#include <memory>
#include <string>

namespace gst_video_context {

/**
 * Dynamically links and load up the required libraries; needs to be called once.
 */
bool init();

struct GstVideoContext;
using gst_context_ptr = std::shared_ptr<GstVideoContext>;

/**
 * Given a GstMessage will automatically set the context if it's a GST_MESSAGE_NEED_CONTEXT
 * and we support the required context type.
 *
 * Returns a smart pointer to the created context, it's up to the caller to store it
 * properly for the duration of the pipeline. Returns nullptr if we haven't created any context.
 */
gst_context_ptr need_context_for_device(const std::string &device_path, GstMessage *msg);

bool is_context_valid(const gst_context_ptr &context);
bool set_context(gst_context_ptr context, GstMessage *msg);

} // namespace gst_video_context
