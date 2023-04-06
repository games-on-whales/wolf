#pragma once
#include <immer/array.hpp>
#include <immer/vector.hpp>
#include <memory>
#include <streaming/data-structures.hpp>

namespace streaming {

typedef struct WaylandState WaylandState;

static void destroy(WaylandState *w_state);
using wl_state_ptr = std::shared_ptr<WaylandState>;

struct GstAppDataState {
  gst_element_ptr app_src;
  wl_state_ptr wayland_state;
  guint source_id{};
  int framerate;
  GstClockTime timestamp = 0;
};

wl_state_ptr create_wayland_display(const immer::array<std::string> &input_devices,
                                    const std::string &render_node = "/dev/dri/renderD128");

std::unique_ptr<GstCaps, decltype(&gst_caps_unref)> set_resolution(WaylandState &w_state,
                                                                   const moonlight::DisplayMode &display_mode,
                                                                   const std::optional<gst_element_ptr> &app_src = {});

immer::vector<std::string> get_devices(const WaylandState &w_state);
immer::vector<std::string> get_env(const WaylandState &w_state);

GstBuffer *get_frame(WaylandState &w_state);
} // namespace streaming