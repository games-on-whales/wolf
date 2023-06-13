#include <streaming/virtual-display.hpp>

namespace streaming {

struct WaylandState {};

std::shared_ptr<WaylandState> create_wayland_display(const immer::array<std::string> &input_devices,
                                                     const std::string &render_node) {
  return {};
}

std::unique_ptr<GstCaps, decltype(&gst_caps_unref)> set_resolution(WaylandState &w_state,
                                                                   const moonlight::DisplayMode &display_mode,
                                                                   const std::optional<gst_element_ptr> &app_src) {
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

} // namespace streaming