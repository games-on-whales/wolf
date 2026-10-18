/**
 * Instead of directly linking to the Rust C bindings, we can interact with the Gstreamer plugin
 */

#include <core/virtual-display.hpp>
#include <linux/input-event-codes.h>
#include <set>

namespace wolf::core::virtual_display {
struct WaylandState {
  /**
   * The wayland plugin element,
   * we need a reference so that we can send events directly to it (mouse, keyboard, ...)
   */
  gstreamer::gst_element_ptr wayland_plugin;

  std::string wayland_socket_name;

  /**
   * A list of currently pressed keyboard keycodes
   */
  std::set<unsigned int> pressed_keys;

  /**
   * A list of currently pressed mouse buttons
   */
  std::set<unsigned int> pressed_buttons;
};

wl_state_ptr create_wayland_display(gstreamer::gst_element_ptr wayland_plugin, const std::string &wayland_socket_name) {
  return std::make_shared<WaylandState>(
      WaylandState{.wayland_plugin = wayland_plugin, .wayland_socket_name = wayland_socket_name});
}

std::string get_wayland_socket_name(WaylandState &w_state) {
  return w_state.wayland_socket_name;
}

bool add_input_device(WaylandState &w_state, const std::string &device_path) {
  gstreamer::send_message(w_state.wayland_plugin.get(),
                          gst_structure_new("VirtualDevicesReady", "path", G_TYPE_STRING, device_path.c_str(), NULL));
  return true;
}

void WaylandMouse::move(int delta_x, int delta_y) {
  auto msg = /* clang-format off */
                          gst_structure_new("MouseMoveRelative",
                            "pointer_x", G_TYPE_DOUBLE, static_cast<double>(delta_x),
                            "pointer_y", G_TYPE_DOUBLE, static_cast<double>(delta_y),
                            NULL);
  /* clang-format on */
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandMouse::move_abs(int x, int y, int screen_width, int screen_height) {
  auto msg = /* clang-format off */
                          gst_structure_new("MouseMoveAbsolute",
                            "pointer_x", G_TYPE_DOUBLE, static_cast<double>(x),
                            "pointer_y", G_TYPE_DOUBLE, static_cast<double>(y),
                            NULL);
  /* clang-format on */
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
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

WaylandMouse::~WaylandMouse() {
  if (w_state) {
    // Release all currently pressed buttons
    // We have to copy here because we are erasing from the set
    std::vector keys_to_release(w_state->pressed_buttons.begin(), w_state->pressed_buttons.end());
    for (const auto key : keys_to_release) {
      release(key);
    }
  }
}

void WaylandMouse::press(unsigned int button) {
  w_state->pressed_buttons.insert(button);
  auto msg = /* clang-format off */
                          gst_structure_new("MouseButton",
                            "button", G_TYPE_UINT, moonlight_button_to_linux(button),
                            "pressed", G_TYPE_BOOLEAN, true,
                            NULL);
  /* clang-format on */
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandMouse::release(unsigned int button) {
  w_state->pressed_buttons.erase(button);
  auto msg = /* clang-format off */
                          gst_structure_new("MouseButton",
                            "button", G_TYPE_UINT, moonlight_button_to_linux(button),
                            "pressed", G_TYPE_BOOLEAN, false,
                            NULL);
  /* clang-format on */
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}
void WaylandMouse::vertical_scroll(int high_res_distance) {
  auto msg = /* clang-format off */
                          gst_structure_new("MouseAxis",
                          "x", G_TYPE_DOUBLE, static_cast<double>(0),
                          "y", G_TYPE_DOUBLE, static_cast<double>(-high_res_distance),
                          NULL);
  /* clang-format on */
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandMouse::horizontal_scroll(int high_res_distance) {
  auto msg = /* clang-format off */
                          gst_structure_new("MouseAxis",
                          "x", G_TYPE_DOUBLE, static_cast<double>(high_res_distance),
                          "y", G_TYPE_DOUBLE, static_cast<double>(0),
                          NULL);
  /* clang-format on */
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
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

WaylandKeyboard::~WaylandKeyboard() {
  if (w_state) {
    // Release all currently pressed keys
    // We have to copy here because we are erasing from the set
    std::vector keys_to_release(w_state->pressed_keys.begin(), w_state->pressed_keys.end());
    for (const auto key : keys_to_release) {
      release(key);
    }
  }
}

void WaylandKeyboard::press(unsigned int key_code) {
  if (key_mappings.contains(key_code)) {
    w_state->pressed_keys.insert(key_code);

    auto msg = /* clang-format off */
                           gst_structure_new("KeyboardKey",
                             "key", G_TYPE_UINT, key_mappings.at(key_code),
                             "pressed", G_TYPE_BOOLEAN, true,
                             NULL);
    /* clang-format on */
    gstreamer::send_message(w_state->wayland_plugin.get(), msg);
  } else {
    logs::log(logs::warning, "Key code not found: {}", key_code);
  }
}

void WaylandKeyboard::release(unsigned int key_code) {
  if (key_mappings.contains(key_code)) {
    // update pressed_keys
    w_state->pressed_keys.erase(key_code);

    auto msg = /* clang-format off */
                           gst_structure_new("KeyboardKey",
                             "key", G_TYPE_UINT, key_mappings.at(key_code),
                             "pressed", G_TYPE_BOOLEAN, false,
                             NULL);
    /* clang-format on */
    gstreamer::send_message(w_state->wayland_plugin.get(), msg);
  } else {
    logs::log(logs::warning, "Key code not found: {}", key_code);
  }
}

WaylandTouchScreen::~WaylandTouchScreen() {
  if (w_state) {
    cancel();
  }
}

void WaylandTouchScreen::down(unsigned int touch_id, double x, double y) {
  auto msg =
      gst_structure_new("TouchDown", "x", G_TYPE_DOUBLE, x, "y", G_TYPE_DOUBLE, y, "id", G_TYPE_UINT, touch_id, NULL);
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandTouchScreen::up(unsigned int touch_id) {
  auto msg = gst_structure_new("TouchUp", "id", G_TYPE_UINT, touch_id, NULL);
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandTouchScreen::motion(unsigned int touch_id, double x, double y) {
  auto msg =
      gst_structure_new("TouchMotion", "x", G_TYPE_DOUBLE, x, "y", G_TYPE_DOUBLE, y, "id", G_TYPE_UINT, touch_id, NULL);
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandTouchScreen::cancel() {
  auto msg = gst_structure_new("TouchCancel", "placeholder", G_TYPE_BOOLEAN, true, NULL);
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

void WaylandTouchScreen::frame() {
  auto msg = gst_structure_new("TouchFrame", "placeholder", G_TYPE_BOOLEAN, true, NULL);
  gstreamer::send_message(w_state->wayland_plugin.get(), msg);
}

} // namespace wolf::core::virtual_display