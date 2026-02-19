/**
 * fake-uinput: LD_PRELOAD library that intercepts uinput ioctl() calls
 * and delegates device node creation/removal to a broker daemon over
 * a Unix socket, instead of calling mknod() directly (which requires
 * CAP_MKNOD inside containers).
 *
 * Protocol with broker at /run/wolf-broker.sock:
 *   Send: MKNOD <sysfs_path>\n        Recv: OK <devname>\n | ERR <msg>\n
 *   Send: REMOVE <sysfs_path>\n       Recv: OK\n          | ERR <msg>\n
 */

#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <linux/uinput.h>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static constexpr const char *BROKER_SOCK = "/run/wolf-broker.sock";
static constexpr const char *TAG = "[fake-uinput]";
static constexpr int MAX_BACKOFF_MS = 5000;
static constexpr int INITIAL_BACKOFF_MS = 10;

using ioctl_fn = int (*)(int, unsigned long, ...);
using close_fn = int (*)(int);
static ioctl_fn real_ioctl = nullptr;
static close_fn real_close = nullptr;

static std::mutex g_mutex;
static std::unordered_map<int, std::vector<std::string>> g_tracked;

static void resolve_symbols() {
    if (!real_ioctl) {
        real_ioctl = reinterpret_cast<ioctl_fn>(dlsym(RTLD_NEXT, "ioctl"));
    }
    if (!real_close) {
        real_close = reinterpret_cast<close_fn>(dlsym(RTLD_NEXT, "close"));
    }
}

/** Connect to the broker with exponential backoff. Returns fd or -1. */
static int broker_connect() {
    int total_ms = 0, backoff_ms = INITIAL_BACKOFF_MS;
    while (total_ms < MAX_BACKOFF_MS) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            return -1;
        }

        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, BROKER_SOCK, sizeof(addr.sun_path) - 1);

        if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr),
                    sizeof(addr)) == 0) {
            return sock;
        }
        real_close(sock);

        usleep(static_cast<useconds_t>(backoff_ms) * 1000);
        total_ms += backoff_ms;
        backoff_ms *= 2;
    }
    return -1;
}

/** Send a command to the broker. Returns true if broker replied "OK...". */
static bool broker_request(const std::string &command) {
    int sock = broker_connect();
    if (sock < 0) {
        std::cout << TAG << " broker unreachable, skipping: "
                  << command.substr(0, command.size() - 1) << std::endl;
        return false;
    }

    ssize_t sent = write(sock, command.data(), command.size());
    if (sent < 0) {
        std::cout << TAG << " send failed: " << strerror(errno) << std::endl;
        real_close(sock);
        return false;
    }

    char buf[256] = {};
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    real_close(sock);

    if (n <= 0) {
        std::cout << TAG << " no response from broker" << std::endl;
        return false;
    }

    std::string resp(buf, static_cast<size_t>(n));
    if (!resp.empty() && resp.back() == '\n') {
        resp.pop_back();
    }

    bool ok = resp.rfind("OK", 0) == 0;
    if (!ok) {
        std::cout << TAG << " broker error: " << resp << std::endl;
    }
    return ok;
}

/** Send REMOVE for a list of tracked sysfs paths. */
static void send_removes(const std::vector<std::string> &paths) {
    for (const auto &p : paths) {
        std::cout << TAG << " requesting: REMOVE " << p << std::endl;
        broker_request("REMOVE " + p + "\n");
    }
}

/**
 * After UI_DEV_CREATE succeeds, discover event/js nodes under the sysfs
 * input directory and ask the broker to create device nodes for each.
 */
static void handle_dev_create(int fd) {
    resolve_symbols();

    char sysname[64] = {};
    if (real_ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0) {
        std::cout << TAG << " UI_GET_SYSNAME failed: " << strerror(errno)
                  << std::endl;
        return;
    }

    std::string sysdir = std::string("/sys/devices/virtual/input/") + sysname;
    std::error_code ec;
    if (!fs::is_directory(sysdir, ec)) {
        std::cout << TAG << " sysfs dir not found: " << sysdir << std::endl;
        return;
    }

    std::vector<std::string> paths;
    for (const auto &entry : fs::directory_iterator(sysdir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0 && name.rfind("js", 0) != 0) {
            continue;
        }

        std::string subpath = entry.path().string();
        std::cout << TAG << " requesting: MKNOD " << subpath << std::endl;
        broker_request("MKNOD " + subpath + "\n");
        paths.push_back(subpath);
    }

    if (!paths.empty()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_tracked[fd] = std::move(paths);
    }
}

/** Send REMOVE for all tracked paths on a given fd, then erase tracking. */
static void handle_dev_destroy(int fd) {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_tracked.find(fd);
        if (it == g_tracked.end()) {
            return;
        }
        paths = std::move(it->second);
        g_tracked.erase(it);
    }
    send_removes(paths);
}

// -- Intercepted libc functions -----------------------------------------------

extern "C" int ioctl(int fd, unsigned long request, ...) {
    resolve_symbols();

    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);

    int result = real_ioctl(fd, request, arg);

    if (result >= 0) {
        if (request == UI_DEV_CREATE) {
            handle_dev_create(fd);
        } else if (request == UI_DEV_DESTROY) {
            handle_dev_destroy(fd);
        }
    }
    return result;
}

extern "C" int close(int fd) {
    resolve_symbols();

    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_tracked.find(fd);
        if (it != g_tracked.end()) {
            paths = std::move(it->second);
            g_tracked.erase(it);
        }
    }
    if (!paths.empty()) {
        std::cout << TAG << " close() cleanup for fd " << fd << std::endl;
        send_removes(paths);
    }
    return real_close(fd);
}
