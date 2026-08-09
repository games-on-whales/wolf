// fake-uinput: an LD_PRELOAD shim that lets an app create its own uinput devices inside Wolf's
// unprivileged session containers -- the typical case being Steam Input. It hooks only
// ioctl(UI_DEV_CREATE / UI_DEV_DESTROY); everything else passes straight through, so Wolf's own
// virtual controllers are untouched. It does no privileged work itself: on create/destroy it
// notifies Wolf over the control socket (HTTP over the AF_UNIX socket at $WOLF_SOCKET_PATH), and
// Wolf does the mknod + fake-udev on the host via the same path it uses for its own virtual pads.
// So the container needs no privilege, not even CAP_MKNOD.
//
// Wolf sets these in the container's env:
//   WOLF_SOCKET_PATH  - the wolf.sock control API
//   WOLF_SESSION_ID   - identifies the session so Wolf injects into the right container
//
// Built 64- and 32-bit (Steam's client is 32-bit); both are LD_PRELOAD'd and the loader skips the
// wrong-arch one. See docs/modules/dev/pages/fake-uinput.adoc.

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <linux/uinput.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SYS_INPUT_DIR "/sys/devices/virtual/input/"
#define PLUG_PATH "/api/v1/runtime/plug-udev-device"
#define UNPLUG_PATH "/api/v1/runtime/unplug-udev-device"

// Stamped on every device the shim creates so 85-wolf.rules can find them. The app picks the device
// NAME (Steam Input calls its pad "Microsoft X-Box 360 pad"), so the "Wolf *virtual*" name rules do
// not match these, and renaming is not an option: SDL looks controllers up by name to pick a button
// mapping. phys is free for us to use, and udev matches it with ATTRS{phys}.
#define SHIM_PHYS "wolf-fake-uinput/0"

typedef int (*ioctl_t)(int, unsigned long, ...);
static ioctl_t real_ioctl = nullptr;

static bool load_library() {
  real_ioctl = reinterpret_cast<ioctl_t>(dlsym(RTLD_NEXT, "ioctl"));
  return real_ioctl != nullptr;
}

// pure C stdio -- safe from an LD_PRELOAD constructor (before libstdc++ iostreams exist)
static void logline(const std::string &msg) {
  char ts[32] = "?";
  struct timespec tp {};
  clock_gettime(CLOCK_REALTIME, &tp);
  time_t t = tp.tv_sec;
  struct tm tm {};
  if (gmtime_r(&t, &tm))
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
  char comm[64] = "";
  if (FILE *cf = fopen("/proc/self/comm", "r")) {
    if (fgets(comm, sizeof(comm), cf)) {
      size_t l = strlen(comm);
      if (l && comm[l - 1] == '\n')
        comm[l - 1] = '\0';
    }
    fclose(cf);
  }
  char hdr[160];
  snprintf(hdr, sizeof(hdr), "%s [fake-uinput] pid=%d (%s) ", ts, (int)getpid(), comm);
  // Always log to stderr (captured in the container logs). Optionally also append to a file when
  // FAKE_UINPUT_LOG points at one -- useful for debugging, off by default (no hardcoded paths).
  if (const char *lp = getenv("FAKE_UINPUT_LOG")) {
    if (FILE *f = fopen(lp, "a")) {
      fprintf(f, "%s%s\n", hdr, msg.c_str());
      fclose(f);
    }
  }
  fprintf(stderr, "%s%s\n", hdr, msg.c_str());
}

static std::string json_escape(const std::string &s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\')
      o += '\\';
    o += c;
  }
  return o;
}

// Minimal HTTP/1.1 POST over the AF_UNIX control socket. Returns the numeric status
// (e.g. 200) or -1 on transport failure. Short timeouts so a wedged Wolf never blocks Steam.
static int wolf_post(const std::string &path, const std::string &body) {
  const char *sock = getenv("WOLF_SOCKET_PATH");
  if (!sock || !*sock) {
    logline("WARN WOLF_SOCKET_PATH unset; cannot notify Wolf");
    return -1;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  struct timeval tv {};
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
  if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
    logline(std::string("WARN connect(") + sock + ") failed: " + strerror(errno));
    close(fd);
    return -1;
  }

  std::string req = "POST " + path +
                    " HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n"
                    "Connection: close\r\n\r\n" +
                    body;

  size_t off = 0;
  while (off < req.size()) {
    ssize_t w = write(fd, req.data() + off, req.size() - off);
    if (w <= 0) {
      close(fd);
      return -1;
    }
    off += (size_t)w;
  }

  // Parse just the status code from "HTTP/1.1 <code> ..."
  char buf[256] = {0};
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return -1;
  int code = -1;
  if (strncmp(buf, "HTTP/1.", 7) == 0) {
    const char *sp = strchr(buf, ' ');
    if (sp)
      code = atoi(sp + 1);
  }
  return code;
}

static void notify(const std::string &path, const std::string &sysname) {
  const char *sid = getenv("WOLF_SESSION_ID");
  std::string body = std::string("{\"session_id\":\"") + json_escape(sid ? sid : "") + "\",\"sysfs_name\":\"" +
                     json_escape(sysname) + "\"}";
  int code = wolf_post(path, body);
  // Log failures always; a successful plug/unplug only under FAKE_UINPUT_DEBUG (Steam creates and
  // tears down its pad on every focus change, so per-event logging would be very noisy otherwise).
  const char *dbg = getenv("FAKE_UINPUT_DEBUG");
  if (code != 200 || (dbg && dbg[0] == '1'))
    logline(std::string(path == PLUG_PATH ? "plug" : "unplug") + " sysfs=" + sysname +
            " session=" + (sid ? sid : "(unset)") + " -> wolf http=" + std::to_string(code));
}

static bool get_sysname(int fd, char *out, size_t cap) {
  // out must be prefilled with SYS_INPUT_DIR; we append the sysname after it.
  size_t pfx = strlen(SYS_INPUT_DIR);
  return real_ioctl(fd, UI_GET_SYSNAME(cap - pfx), out + pfx) != -1;
}

extern "C" int ioctl(int fd, unsigned long request, ...) {
  if (!real_ioctl && !load_library())
    return -1;

  va_list args;
  va_start(args, request);
  void *arg = va_arg(args, void *);
  va_end(args);

  // For DESTROY, resolve the sysname BEFORE the real ioctl (it's gone afterwards).
  char destroy_buf[sizeof(SYS_INPUT_DIR) + 64] = SYS_INPUT_DIR;
  bool have_destroy = false;
  if (request == UI_DEV_DESTROY)
    have_destroy = get_sysname(fd, destroy_buf, sizeof(destroy_buf));

  // Has to happen before the device exists, so it is set here rather than after the create below.
  // A failure is not fatal: the device still works, it just will not be caught by the udev rule.
  if (request == UI_DEV_CREATE && real_ioctl(fd, UI_SET_PHYS, (void *)SHIM_PHYS) < 0)
    logline(std::string("WARN UI_SET_PHYS failed: ") + strerror(errno));

  int result = real_ioctl(fd, request, arg);

  if (result >= 0 && request == UI_DEV_CREATE) {
    char buf[sizeof(SYS_INPUT_DIR) + 64] = SYS_INPUT_DIR;
    if (get_sysname(fd, buf, sizeof(buf)))
      notify(PLUG_PATH, buf + strlen(SYS_INPUT_DIR));
    else
      logline("UI_DEV_CREATE but UI_GET_SYSNAME failed");
  } else if (result >= 0 && request == UI_DEV_DESTROY && have_destroy) {
    notify(UNPLUG_PATH, destroy_buf + strlen(SYS_INPUT_DIR));
  }

  return result;
}

__attribute__((constructor)) static void on_load() {
  if (const char *d = getenv("FAKE_UINPUT_DEBUG"); d && d[0] == '1')
    logline("shim loaded (wolf-native)");
}
