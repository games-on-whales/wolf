#pragma once

#include <string_view>

namespace vulkan_probe {

/**
 * Whether the GPU behind `render_node` (for example "/dev/dri/renderD129") can encode
 * `codec` ("h264", "h265" or "av1") through the Vulkan video encode queue.
 *
 * This is the per-GPU check gst-vulkan's plugin init does when registering the
 * vulkanh264enc/vulkanh265enc/vulkanav1enc elements: on multi-GPU hosts those register
 * when ANY device supports the codec, so the wrong GPU can be selected. This probe
 * answers the question for one specific render node.
 *
 * Returns false when the Vulkan GStreamer library is unavailable, the GPU cannot be
 * found, or it cannot encode the codec: callers treat that as "skip this encoder".
 */
bool supports_video_encode(std::string_view render_node, std::string_view codec);

} // namespace vulkan_probe
