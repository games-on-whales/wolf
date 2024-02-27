#include <core/virtual-display.hpp>

namespace wolf::core::virtual_display {

struct WaylandState {};

std::shared_ptr<WaylandState> create_wayland_display(const immer::array<std::string> &input_devices,
                                                     const std::string &render_node) {
  return {};
}

std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>
set_resolution(WaylandState &w_state, const DisplayMode &display_mode, const std::optional<gst_element_ptr> &app_src) {
  return {nullptr, gst_caps_unref};
}

immer::vector<std::string> get_devices(const WaylandState &w_state) {
  return {};
}

immer::vector<std::string> get_env(const WaylandState &w_state) {
  return {};
}

GstBuffer *get_frame(WaylandState &w_state) {
  return nullptr;
}

bool add_input_device(WaylandState &w_state, const std::string &device_path) {
  return false;
}

} // namespace wolf::core::virtual_display