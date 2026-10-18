#pragma once
#include <core/gstreamer.hpp>
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

wl_state_ptr create_wayland_display(gstreamer::gst_element_ptr wayland_plugin, const std::string &wayland_socket_name);

std::string get_wayland_socket_name(WaylandState &w_state);

bool add_input_device(WaylandState &w_state, const std::string &device_path);

class WaylandMouse {
public:
  WaylandMouse(wl_state_ptr w_state) : w_state(w_state) {};
  WaylandMouse(WaylandMouse &&j) noexcept : w_state(nullptr) {
    std::swap(j.w_state, w_state);
  }
  ~WaylandMouse();

  void move(int delta_x, int delta_y);

  void move_abs(int x, int y, int screen_width, int screen_height);

  void press(unsigned int button);

  void release(unsigned int button);

  void vertical_scroll(int high_res_distance);

  void horizontal_scroll(int high_res_distance);

private:
  wl_state_ptr w_state;
};

class WaylandKeyboard {
public:
  WaylandKeyboard(wl_state_ptr w_state) : w_state(w_state) {};
  WaylandKeyboard(WaylandKeyboard &&j) noexcept : w_state(nullptr) {
    std::swap(j.w_state, w_state);
  }
  ~WaylandKeyboard();

  void press(unsigned int key_code);

  void release(unsigned int key_code);

private:
  wl_state_ptr w_state;
};

class WaylandTouchScreen {
public:
  WaylandTouchScreen(wl_state_ptr w_state) : w_state(w_state) {};
  WaylandTouchScreen(WaylandTouchScreen &&j) noexcept : w_state(nullptr) {
    std::swap(j.w_state, w_state);
  }
  ~WaylandTouchScreen();

  void down(unsigned int touch_id, double x, double y);

  void up(unsigned int touch_id);

  void motion(unsigned int touch_id, double x, double y);

  void cancel();

  void frame();

private:
  wl_state_ptr w_state;
};

} // namespace wolf::core::virtual_display