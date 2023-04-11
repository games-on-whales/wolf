#include <streaming/virtual-display.hpp>

namespace streaming {

struct WaylandState {};

std::shared_ptr<WaylandState> create_wayland_display(const immer::array<std::string> &input_devices,
                                                     const std::string &render_node) {
  return {};
}

std::shared_ptr<GstCaps> set_resolution(const std::shared_ptr<WaylandState> &w_state,
                                        const moonlight::DisplayMode &display_mode,
                                        const std::optional<gst_element_ptr> &app_src) {
  return {};
}

immer::vector<std::string> get_devices(const std::shared_ptr<WaylandState> &w_state) {
  return {};
}
immer::vector<std::string> get_env(const std::shared_ptr<WaylandState> &w_state) {
  return {};
}

GstBuffer *get_frame(const std::shared_ptr<WaylandState> &w_state) {
  return nullptr;
}

} // namespace streaming