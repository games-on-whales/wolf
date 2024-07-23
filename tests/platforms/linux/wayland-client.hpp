#pragma once

extern "C" {
#include "wayland-relative-pointer-client-protocol.h"
#include "wayland-xdg-shell-client-protocol.h"
}
#include <catch2/catch_test_macros.hpp>
#include <core/virtual-display.hpp>
#include <errno.h>
#include <fcntl.h>
#include <helpers/logger.hpp>
#include <helpers/tsqueue.hpp>
#include <helpers/utils.hpp>
#include <memory>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

namespace wolf::core::virtual_display {

struct WClientState { // The trick here to use shared_ptr is so that it'll automatically call the destroy function
  std::shared_ptr<wl_seat> seat = {};
  std::shared_ptr<wl_compositor> compositor = {};
  std::shared_ptr<wl_shm> shm = {};
  std::shared_ptr<xdg_wm_base> xwm_base = {};
  std::shared_ptr<zwp_relative_pointer_manager_v1> relative_pointer_manager = {};

  std::shared_ptr<wl_surface> surface = {};
  std::shared_ptr<xdg_surface> xsurface = {};

  std::shared_ptr<wl_keyboard> keyboard = {};
  std::shared_ptr<wl_pointer> pointer = {};
  std::shared_ptr<zwp_relative_pointer_v1> relative_pointer = {};
};

constexpr int WINDOW_WIDTH = 640;
constexpr int WINDOW_HEIGHT = 480;

std::shared_ptr<wl_display> w_connect(std::shared_ptr<WaylandState> w_state) {
  auto display_name = utils::split(get_env(*w_state)[0], '=')[1];
  auto display = wl_display_connect(display_name.data());
  REQUIRE(display != nullptr);
  return std::shared_ptr<wl_display>(display, &wl_display_disconnect);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = [](void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
      xdg_wm_base_pong(xdg_wm_base, serial);
    }};

std::shared_ptr<WClientState> w_get_state(std::shared_ptr<wl_display> wd) {
  struct wl_registry *registry = wl_display_get_registry(wd.get());
  struct wl_registry_listener listener = {
      [](void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
        logs::log(logs::debug, "Got registry event: id={}, interface={}, version={}", id, interface, version);
        auto state = (WClientState *)data;
        if (strcmp(interface, "wl_seat") == 0) {
          state->seat = std::shared_ptr<wl_seat>((wl_seat *)wl_registry_bind(registry, id, &wl_seat_interface, version),
                                                 &wl_seat_destroy);
        } else if (strcmp(interface, "wl_compositor") == 0) {
          state->compositor = std::shared_ptr<wl_compositor>(
              (wl_compositor *)wl_registry_bind(registry, id, &wl_compositor_interface, version),
              &wl_compositor_destroy);
        } else if (strcmp(interface, "wl_shm") == 0) {
          state->shm = std::shared_ptr<wl_shm>((wl_shm *)wl_registry_bind(registry, id, &wl_shm_interface, version),
                                               &wl_shm_destroy);
        } else if (strcmp(interface, "xdg_wm_base") == 0) {
          state->xwm_base = std::shared_ptr<xdg_wm_base>(
              (xdg_wm_base *)wl_registry_bind(registry, id, &xdg_wm_base_interface, version),
              &xdg_wm_base_destroy);
          xdg_wm_base_add_listener(state->xwm_base.get(), &xdg_wm_base_listener, state);
        } else if (strcmp(interface, "zwp_relative_pointer_manager_v1") == 0) {
          state->relative_pointer_manager = std::shared_ptr<zwp_relative_pointer_manager_v1>(
              (zwp_relative_pointer_manager_v1 *)
                  wl_registry_bind(registry, id, &zwp_relative_pointer_manager_v1_interface, version),
              &zwp_relative_pointer_manager_v1_destroy);
        }
      },
      [](void *data, struct wl_registry *registry, uint32_t id) {}};

  auto w_state = std::make_shared<WClientState>();
  wl_registry_add_listener(registry, &listener, w_state.get());
  wl_display_dispatch(wd.get());
  wl_display_roundtrip(wd.get());

  REQUIRE(w_state->seat != nullptr);
  REQUIRE(w_state->compositor != nullptr);
  REQUIRE(w_state->shm != nullptr);

  return w_state;
}

// SHM helpers, taken from: https://wayland-book.com/surfaces/shared-memory.html

static void randname(char *buf) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  long r = ts.tv_nsec;
  for (int i = 0; i < 6; ++i) {
    buf[i] = 'A' + (r & 15) + (r & 16) * 2;
    r >>= 5;
  }
}

static int create_shm_file(void) {
  int retries = 100;
  do {
    char name[] = "/wl_shm-XXXXXX";
    randname(name + sizeof(name) - 7);
    --retries;
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink(name);
      return fd;
    }
  } while (retries > 0 && errno == EEXIST);
  return -1;
}

int allocate_shm_file(size_t size) {
  int fd = create_shm_file();
  if (fd < 0)
    return -1;
  int ret;
  do {
    ret = ftruncate(fd, size);
  } while (ret < 0 && errno == EINTR);
  if (ret < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = [](void *data, struct wl_buffer *wl_buffer) { wl_buffer_destroy(wl_buffer); },
};

static struct wl_buffer *draw_frame(WClientState *state) {
  int stride = WINDOW_WIDTH * 4;
  int size = stride * WINDOW_HEIGHT;

  int fd = allocate_shm_file(size);
  if (fd == -1) {
    return NULL;
  }

  uint32_t *data = static_cast<uint32_t *>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (data == MAP_FAILED) {
    close(fd);
    return NULL;
  }

  struct wl_shm_pool *pool = wl_shm_create_pool(state->shm.get(), fd, size);
  struct wl_buffer *buffer =
      wl_shm_pool_create_buffer(pool, 0, WINDOW_WIDTH, WINDOW_HEIGHT, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  /* Draw checkerboxed background */
  for (int y = 0; y < WINDOW_HEIGHT; ++y) {
    for (int x = 0; x < WINDOW_WIDTH; ++x) {
      if ((x + y / 8 * 8) % 16 < 8)
        data[y * WINDOW_WIDTH + x] = 0xFF666666;
      else
        data[y * WINDOW_WIDTH + x] = 0xFFEEEEEE;
    }
  }

  munmap(data, size);
  wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
  return buffer;
}

static void commit_frame(WClientState *state) {
  struct wl_buffer *buffer = draw_frame(state);
  wl_surface_attach(state->surface.get(), buffer, 0, 0);
  wl_surface_damage_buffer(state->surface.get(), 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(state->surface.get());
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure =
        [](void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
          WClientState *state = static_cast<WClientState *>(data);
          xdg_surface_ack_configure(xdg_surface, serial);
          commit_frame(state);
        },
};

void w_display_create_window(WClientState &w_state) {
  auto surface = wl_compositor_create_surface(w_state.compositor.get());
  w_state.surface = std::shared_ptr<wl_surface>(surface, &wl_surface_destroy);

  // xdg_surface
  auto xdg_surface_ptr = xdg_wm_base_get_xdg_surface(w_state.xwm_base.get(), surface);
  w_state.xsurface = std::shared_ptr<xdg_surface>(xdg_surface_ptr, &xdg_surface_destroy);
  xdg_surface_add_listener(xdg_surface_ptr, &xdg_surface_listener, &w_state);

  auto xdg_toplevel = xdg_surface_get_toplevel(w_state.xsurface.get());
  xdg_toplevel_set_title(xdg_toplevel, "Wolf Wayland Client");
  xdg_toplevel_set_app_id(xdg_toplevel, "wolf-client");

  wl_surface_commit(surface);
}

// The "wl_array_for_each" C macro for C++
// https://github.com/libretro/RetroArch/blob/a9125fffaa981cab811ba6caf4d756fa6ef9a561/input/common/wayland_common.h#L50-L53
#define WL_ARRAY_FOR_EACH(pos, array, type)                                                                            \
  for (pos = (type)(array)->data; (const char *)pos < ((const char *)(array)->data + (array)->size); (pos)++)

struct KeyEvent {
  uint32_t keycode;
  bool pressed;
};

static const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap =
        [](void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
          logs::log(logs::debug, "[KEYBOARD] keymap event: format={}, fd={}, size={}", format, fd, size);
        },
    .enter =
        [](void *data,
           struct wl_keyboard *wl_keyboard,
           uint32_t serial,
           struct wl_surface *surface,
           struct wl_array *keys) {
          logs::log(logs::debug, "[KEYBOARD] Got enter event: serial={}", serial);
          auto queue = static_cast<TSQueue<KeyEvent> *>(data);
          uint32_t *key;
          WL_ARRAY_FOR_EACH(key, keys, unsigned int *) {
            queue->push({*key, true});
          }
        },
    .leave =
        [](void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
          logs::log(logs::debug, "[KEYBOARD] Got leave event: serial={}", serial);
        },
    .key =
        [](void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
          logs::log(logs::debug, "[KEYBOARD] Got key event: time={}, key={}, state={}", time, key, state);
          auto queue = static_cast<TSQueue<KeyEvent> *>(data);
          queue->push({key, state == WL_KEYBOARD_KEY_STATE_PRESSED});
        },
    .modifiers =
        [](void *data,
           struct wl_keyboard *wl_keyboard,
           uint32_t serial,
           uint32_t mods_depressed,
           uint32_t mods_latched,
           uint32_t mods_locked,
           uint32_t group) {
          logs::log(logs::debug,
                    "[KEYBOARD] Got modifiers event: mods_depressed={}, mods_latched={}, mods_locked={}",
                    mods_depressed,
                    mods_latched,
                    mods_locked);
        },
    .repeat_info =
        [](void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
          logs::log(logs::debug, "[KEYBOARD] Got repeat info event: rate={}, delay={}", rate, delay);
        }};

std::shared_ptr<TSQueue<KeyEvent>> w_get_keyboard_queue(WClientState &w_state) {
  auto w_kb = wl_seat_get_keyboard(w_state.seat.get());
  REQUIRE(w_kb != nullptr);
  w_state.keyboard = std::shared_ptr<wl_keyboard>(w_kb, &wl_keyboard_destroy);
  auto queue = std::make_shared<TSQueue<KeyEvent>>();
  wl_keyboard_add_listener(w_kb, &wl_keyboard_listener, queue.get());
  return queue;
}

enum MouseEventType {
  ENTER,
  LEAVE,
  MOTION,
  RELATIVE_MOTION,
  BUTTON,
  AXIS,
  FRAME,
  AXIS_SOURCE,
  AXIS_STOP,
  AXIS_DISCRETE,
  AXIS_VALUE120
};
struct MouseEvent {
  MouseEventType type;

  wl_fixed_t x;
  wl_fixed_t y;

  uint32_t button;
  bool button_pressed;

  uint32_t axis;
  wl_fixed_t axis_value;
};

static const struct wl_pointer_listener wl_pointer_listener = {
    .enter =
        [](void *data,
           struct wl_pointer *wl_pointer,
           uint32_t serial,
           struct wl_surface *surface,
           wl_fixed_t surface_x,
           wl_fixed_t surface_y) {
          logs::log(logs::debug, "[MOUSE] Got mouse enter event: surface_x={}, surface_y={}", surface_x, surface_y);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::ENTER, .x = surface_x, .y = surface_y});
          // TODO: wl_pointer_set_cursor() here
        },
    .leave =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
          logs::log(logs::debug, "[MOUSE] Got mouse leave event");
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::LEAVE});
        },
    .motion =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
          logs::log(logs::debug, "[MOUSE] Got mouse motion event: surface_x={}, surface_y={}", surface_x, surface_y);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::MOTION, .x = surface_x, .y = surface_y});
        },
    .button =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
          logs::log(logs::debug, "[MOUSE] Got mouse button event: button={}, state={}", button, state);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::BUTTON,
                       .button = button,
                       .button_pressed = state == WL_POINTER_BUTTON_STATE_PRESSED});
        },
    .axis =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
          logs::log(logs::debug, "[MOUSE] Got mouse axis event: axis={}, value={}", axis, value);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::AXIS, .axis = axis, .axis_value = value});
        },
    .frame =
        [](void *data, struct wl_pointer *wl_pointer) {
          logs::log(logs::debug, "[MOUSE] Got mouse frame event");
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::FRAME});
        },
    .axis_source =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
          logs::log(logs::debug, "[MOUSE] Got mouse axis source event: axis_source={}", axis_source);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::AXIS_SOURCE});
        },
    .axis_stop =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
          logs::log(logs::debug, "[MOUSE] Got mouse axis stop event: time={}, axis={}", time, axis);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::AXIS_STOP});
        },
    .axis_discrete =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
          logs::log(logs::debug, "[MOUSE] Got mouse axis discrete event: axis={}, discrete={}", axis, discrete);
          auto queue = static_cast<TSQueue<MouseEvent> *>(data);
          queue->push({.type = MouseEventType::AXIS_DISCRETE});
        }};

static const struct zwp_relative_pointer_v1_listener zwp_relative_pointer_v1_listener = {
    .relative_motion = [](void *data,
                          struct zwp_relative_pointer_v1 *zwp_relative_pointer_v1,
                          uint32_t utime_hi,
                          uint32_t utime_lo,
                          wl_fixed_t dx,
                          wl_fixed_t dy,
                          wl_fixed_t dx_unaccel,
                          wl_fixed_t dy_unaccel) {
      logs::log(logs::debug, "[MOUSE] Got mouse relative motion event: dx={}, dy={}", dx, dy);
      auto queue = static_cast<TSQueue<MouseEvent> *>(data);
      queue->push({.type = MouseEventType::MOTION, .x = dx, .y = dy});
    }};

std::shared_ptr<TSQueue<MouseEvent>> w_get_mouse_queue(WClientState &w_state) {
  auto w_pointer = wl_seat_get_pointer(w_state.seat.get());
  REQUIRE(w_pointer != nullptr);
  w_state.pointer = std::shared_ptr<wl_pointer>(w_pointer, &wl_pointer_destroy);
  auto queue = std::make_shared<TSQueue<MouseEvent>>();
  wl_pointer_add_listener(w_pointer, &wl_pointer_listener, queue.get());

  auto zwp_pointer =
      zwp_relative_pointer_manager_v1_get_relative_pointer(w_state.relative_pointer_manager.get(), w_pointer);
  w_state.relative_pointer = std::shared_ptr<zwp_relative_pointer_v1>(zwp_pointer, &zwp_relative_pointer_v1_destroy);
  zwp_relative_pointer_v1_add_listener(zwp_pointer, &zwp_relative_pointer_v1_listener, queue.get());

  return queue;
}

} // namespace wolf::core::virtual_display