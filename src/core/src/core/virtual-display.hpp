#pragma once
#include <gst/gst.h>
#include <immer/array.hpp>
#include <immer/vector.hpp>
#include <memory>
#include <optional>

namespace wolf::core::virtual_display {

struct DisplayMode {
  int width;
  int height;
  int refreshRate;
};

typedef struct WaylandState WaylandState;

using wl_state_ptr = std::shared_ptr<WaylandState>;
using gst_element_ptr = std::shared_ptr<GstElement>;

wl_state_ptr create_wayland_display(const immer::array<std::string> &input_devices,
                                    const std::string &render_node = "/dev/dri/renderD128");

std::unique_ptr<GstCaps, decltype(&gst_caps_unref)> set_resolution(WaylandState &w_state,
                                                                   const DisplayMode &display_mode,
                                                                   const std::optional<gst_element_ptr> &app_src = {});

immer::vector<std::string> get_devices(const WaylandState &w_state);
immer::vector<std::string> get_env(const WaylandState &w_state);

static void destroy(WaylandState *w_state);
GstBuffer *get_frame(WaylandState &w_state);
} // namespace wolf::core::virtual_display