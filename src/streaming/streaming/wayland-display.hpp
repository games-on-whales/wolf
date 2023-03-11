#pragma once
#include <immer/array.hpp>
#include <immer/vector.hpp>
#include <memory>
#include <streaming/data-structures.hpp>

extern "C" {
#include <waylanddisplay.h>
}

namespace streaming {

struct WaylandState {
  std::shared_ptr<WaylandDisplay> display;
  immer::vector<std::string> env;
  immer::vector<std::string> graphic_devices;
};

std::shared_ptr<WaylandState> create_wayland_display(const immer::array<std::string> &input_devices,
                                                     const std::string &render_node = "/dev/dri/renderD128");

void set_resolution(const std::shared_ptr<WaylandState> &w_state,
                    const moonlight::DisplayMode &display_mode,
                    const std::optional<gst_element_ptr> &app_src = {});

} // namespace streaming