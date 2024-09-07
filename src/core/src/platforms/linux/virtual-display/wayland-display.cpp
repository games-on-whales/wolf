#include <core/virtual-display.hpp>
#include <gst/app/gstappsrc.h>
#include <helpers/logger.hpp>
#include <immer/vector_transient.hpp>
#include <inputtino/protected_types.hpp>

extern "C" {
#include <libgstwaylanddisplay/libgstwaylanddisplay.h>
}

namespace wolf::core::virtual_display {

struct WaylandState {
  WaylandDisplay *display{};
  immer::vector<std::string> env{};
  immer::vector<std::string> graphic_devices{};
};

wl_state_ptr create_wayland_display(const immer::array<std::string> &input_devices, const std::string &render_node) {
  logs::log(logs::debug, "[WAYLAND] Creating wayland display");
  auto w_display = display_init(render_node.c_str());
  immer::vector_transient<std::string> final_devices;
  immer::vector_transient<std::string> final_env;

  for (const auto &device : input_devices) {
    display_add_input_device(w_display, device.c_str());
  }

  { // c-style get devices list
    const unsigned int n_devices = display_get_devices_len(w_display);
    auto strs = std::vector<const char *>(n_devices);
    display_get_devices(w_display, strs.data(), n_devices);
    for (int i = 0; i < n_devices; ++i) {
      final_devices.push_back(strs[i]);
    }
  }

  { // c-style get env list
    const auto n_envs = display_get_envvars_len(w_display);
    auto strs = std::vector<const char *>(n_envs);
    display_get_envvars(w_display, strs.data(), n_envs);
    for (int i = 0; i < n_envs; ++i) {
      final_env.push_back(strs[i]);
    }
  }

  auto wl_state = new WaylandState{.display = w_display,
                                   .env = final_env.persistent(),
                                   .graphic_devices = final_devices.persistent()};
  return {wl_state, &destroy};
}

std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>
set_resolution(WaylandState &w_state, const DisplayMode &display_mode, const std::optional<gst_element_ptr> &app_src) {
  /* clang-format off */
  auto caps = gst_caps_new_simple("video/x-raw",
                                  "width", G_TYPE_INT, display_mode.width,
                                  "height", G_TYPE_INT, display_mode.height,
                                  "framerate", GST_TYPE_FRACTION, display_mode.refreshRate, 1,
                                  "format", G_TYPE_STRING, "RGBx",
                                  NULL);/* clang-format on */

  if (app_src) {
    gst_app_src_set_caps(GST_APP_SRC((app_src.value()).get()), caps);
  }

  auto video_info = gst_video_info_new();
  if (gst_video_info_from_caps(video_info, caps)) {
    display_set_video_info(w_state.display, video_info);
  } else {
    logs::log(logs::warning, "[WAYLAND] Unable to set video_info from caps");
  }

  gst_video_info_free(video_info);
  return {caps, gst_caps_unref};
}

immer::vector<std::string> get_devices(const WaylandState &w_state) {
  return w_state.graphic_devices;
}

immer::vector<std::string> get_env(const WaylandState &w_state) {
  return w_state.env;
}

GstBuffer *get_frame(WaylandState &w_state) {
  return display_get_frame(w_state.display);
}

static void destroy(WaylandState *w_state) {
  logs::log(logs::trace, "~WaylandState");
  display_finish(w_state->display);
}

bool add_input_device(WaylandState &w_state, const std::string &device_path) {
  display_add_input_device(w_state.display, device_path.c_str());
  return true;
}

void WaylandMouse::move(int delta_x, int delta_y) {
  display_pointer_motion(w_state->display, delta_x, delta_y);
}

void WaylandMouse::move_abs(int x, int y, int screen_width, int screen_height) {
  display_pointer_motion_absolute(w_state->display, x, y);
}

unsigned int moonlight_button_to_linux(unsigned int button) {
  switch (button) {
  case 1:
    return BTN_LEFT;
  case 2:
    return BTN_MIDDLE;
  case 3:
    return BTN_RIGHT;
  case 4:
    return BTN_SIDE;
  default:
    return BTN_EXTRA;
  }
}

void WaylandMouse::press(unsigned int button) {
  display_pointer_button(w_state->display, moonlight_button_to_linux(button), true);
}

void WaylandMouse::release(unsigned int button) {
  display_pointer_button(w_state->display, moonlight_button_to_linux(button), false);
}

void WaylandMouse::vertical_scroll(int high_res_distance) {
  display_pointer_axis(w_state->display, 0, -high_res_distance);
}

void WaylandMouse::horizontal_scroll(int high_res_distance) {
  display_pointer_axis(w_state->display, high_res_distance, 0);
}

/**
 * A map of Moonlight key codes to Linux key codes
 */
static const std::map<unsigned int, unsigned int> key_mappings = {
    {0x08, KEY_BACKSPACE},  {0x09, KEY_TAB},
    {0x0C, KEY_CLEAR},      {0x0D, KEY_ENTER},
    {0x10, KEY_LEFTSHIFT},  {0x11, KEY_LEFTCTRL},
    {0x12, KEY_LEFTALT},    {0x13, KEY_PAUSE},
    {0x14, KEY_CAPSLOCK},   {0x15, KEY_KATAKANAHIRAGANA},
    {0x16, KEY_HANGEUL},    {0x17, KEY_HANJA},
    {0x19, KEY_KATAKANA},   {0x1B, KEY_ESC},
    {0x20, KEY_SPACE},      {0x21, KEY_PAGEUP},
    {0x22, KEY_PAGEDOWN},   {0x23, KEY_END},
    {0x24, KEY_HOME},       {0x25, KEY_LEFT},
    {0x26, KEY_UP},         {0x27, KEY_RIGHT},
    {0x28, KEY_DOWN},       {0x29, KEY_SELECT},
    {0x2A, KEY_PRINT},      {0x2C, KEY_SYSRQ},
    {0x2D, KEY_INSERT},     {0x2E, KEY_DELETE},
    {0x2F, KEY_HELP},       {0x30, KEY_0},
    {0x31, KEY_1},          {0x32, KEY_2},
    {0x33, KEY_3},          {0x34, KEY_4},
    {0x35, KEY_5},          {0x36, KEY_6},
    {0x37, KEY_7},          {0x38, KEY_8},
    {0x39, KEY_9},          {0x41, KEY_A},
    {0x42, KEY_B},          {0x43, KEY_C},
    {0x44, KEY_D},          {0x45, KEY_E},
    {0x46, KEY_F},          {0x47, KEY_G},
    {0x48, KEY_H},          {0x49, KEY_I},
    {0x4A, KEY_J},          {0x4B, KEY_K},
    {0x4C, KEY_L},          {0x4D, KEY_M},
    {0x4E, KEY_N},          {0x4F, KEY_O},
    {0x50, KEY_P},          {0x51, KEY_Q},
    {0x52, KEY_R},          {0x53, KEY_S},
    {0x54, KEY_T},          {0x55, KEY_U},
    {0x56, KEY_V},          {0x57, KEY_W},
    {0x58, KEY_X},          {0x59, KEY_Y},
    {0x5A, KEY_Z},          {0x5B, KEY_LEFTMETA},
    {0x5C, KEY_RIGHTMETA},  {0x5F, KEY_SLEEP},
    {0x60, KEY_KP0},        {0x61, KEY_KP1},
    {0x62, KEY_KP2},        {0x63, KEY_KP3},
    {0x64, KEY_KP4},        {0x65, KEY_KP5},
    {0x66, KEY_KP6},        {0x67, KEY_KP7},
    {0x68, KEY_KP8},        {0x69, KEY_KP9},
    {0x6A, KEY_KPASTERISK}, {0x6B, KEY_KPPLUS},
    {0x6C, KEY_KPCOMMA},    {0x6D, KEY_KPMINUS},
    {0x6E, KEY_KPDOT},      {0x6F, KEY_KPSLASH},
    {0x70, KEY_F1},         {0x71, KEY_F2},
    {0x72, KEY_F3},         {0x73, KEY_F4},
    {0x74, KEY_F5},         {0x75, KEY_F6},
    {0x76, KEY_F7},         {0x77, KEY_F8},
    {0x78, KEY_F9},         {0x79, KEY_F10},
    {0x7A, KEY_F11},        {0x7B, KEY_F12},
    {0x90, KEY_NUMLOCK},    {0x91, KEY_SCROLLLOCK},
    {0xA0, KEY_LEFTSHIFT},  {0xA1, KEY_RIGHTSHIFT},
    {0xA2, KEY_LEFTCTRL},   {0xA3, KEY_RIGHTCTRL},
    {0xA4, KEY_LEFTALT},    {0xA5, KEY_RIGHTALT},
    {0xBA, KEY_SEMICOLON},  {0xBB, KEY_EQUAL},
    {0xBC, KEY_COMMA},      {0xBD, KEY_MINUS},
    {0xBE, KEY_DOT},        {0xBF, KEY_SLASH},
    {0xC0, KEY_GRAVE},      {0xDB, KEY_LEFTBRACE},
    {0xDC, KEY_BACKSLASH},  {0xDD, KEY_RIGHTBRACE},
    {0xDE, KEY_APOSTROPHE}, {0xE2, KEY_102ND},
};

void WaylandKeyboard::press(unsigned int key_code) {
  display_keyboard_input(w_state->display, key_mappings.at(key_code), true);
}

void WaylandKeyboard::release(unsigned int key_code) {
  display_keyboard_input(w_state->display, key_mappings.at(key_code), false);
}

} // namespace wolf::core::virtual_display