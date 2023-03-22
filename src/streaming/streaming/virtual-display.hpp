#pragma once
#include <immer/array.hpp>
#include <immer/vector.hpp>
#include <memory>
#include <streaming/data-structures.hpp>

namespace streaming {

typedef struct WaylandState WaylandState;

struct GstAppDataState {
  gst_element_ptr app_src;
  std::shared_ptr<WaylandState> wayland_state;
  guint source_id{};
  int framerate;
  GstClockTime timestamp = 0;
};

std::shared_ptr<WaylandState> create_wayland_display(const immer::array<std::string> &input_devices,
                                                     const std::string &render_node = "/dev/dri/renderD128");

std::shared_ptr<GstCaps> set_resolution(const std::shared_ptr<WaylandState> &w_state,
                                        const moonlight::DisplayMode &display_mode,
                                        const std::optional<gst_element_ptr> &app_src = {});

immer::vector<std::string> get_devices(const std::shared_ptr<WaylandState> &w_state);
immer::vector<std::string> get_env(const std::shared_ptr<WaylandState> &w_state);

GstBuffer *get_frame(const std::shared_ptr<WaylandState> &w_state);

} // namespace streaming