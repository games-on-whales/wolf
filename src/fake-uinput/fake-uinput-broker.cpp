#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static constexpr const char *SOCK_PATH = "/run/wolf-broker.sock";
static constexpr const char *FAKE_UDEV = "/usr/bin/fake-udev";
static constexpr const char *SYSFS_PREFIX = "/sys/devices/virtual/input/";
static constexpr int INPUT_MAJOR = 13;

static std::map<std::string, std::string> tracked_devices;
static int listen_fd = -1;
static volatile sig_atomic_t running = 1;

static void log(const std::string &msg) { std::cout << "[fake-uinput-broker] " << msg << std::endl; }

static std::string base64_encode(const std::string &in) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) { out.push_back(t[(val >> valb) & 0x3F]); valb -= 6; }
  }
  if (valb > -6) out.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

static std::string read_file(const fs::path &path) {
  std::ifstream f(path);
  if (!f) return {};
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

static std::string extract_uevent_field(const std::string &uevent, const std::string &key) {
  std::istringstream ss(uevent);
  std::string line;
  while (std::getline(ss, line))
    if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
  return {};
}

static void fire_udev(const std::string &action, const std::string &devname,
                       const std::string &devpath, int major, int minor, int seqnum) {
  std::string props;
  auto kv = [&](const std::string &k, const std::string &v) {
    props += k + "=" + v;
    props.push_back('\0');
  };
  kv("ACTION", action);
  kv("DEVNAME", devname);
  kv("DEVPATH", devpath);
  kv("SUBSYSTEM", "input");
  kv("MAJOR", std::to_string(major));
  kv("MINOR", std::to_string(minor));
  kv("SEQNUM", std::to_string(seqnum));

  std::string b64 = base64_encode(props);
  pid_t pid = fork();
  if (pid == 0) { execl(FAKE_UDEV, "fake-udev", "-m", b64.c_str(), nullptr); _exit(127); }
  if (pid > 0) waitpid(pid, nullptr, 0);
}

static std::string handle_mknod(const std::string &sysfs_path) {
  static int seqnum = 1000;
  if (sysfs_path.rfind(SYSFS_PREFIX, 0) != 0)
    return "ERR sysfs_path outside allowed prefix\n";

  std::string dev_str = read_file(fs::path(sysfs_path) / "dev");
  std::string uevent = read_file(fs::path(sysfs_path) / "uevent");
  if (dev_str.empty() || uevent.empty())
    return "ERR cannot read dev or uevent from sysfs\n";

  int maj = 0, min = 0;
  if (sscanf(dev_str.c_str(), "%d:%d", &maj, &min) != 2)
    return "ERR malformed dev file\n";
  if (maj != INPUT_MAJOR)
    return "ERR major " + std::to_string(maj) + " is not input (13)\n";

  std::string devname = extract_uevent_field(uevent, "DEVNAME");
  if (devname.empty())
    return "ERR missing DEVNAME in uevent\n";

  static const std::regex devname_re("^input/(event|js|mouse)[0-9]+$");
  if (!std::regex_match(devname, devname_re))
    return "ERR invalid DEVNAME: " + devname + "\n";

  fs::path devnode = fs::path("/dev") / devname;
  fs::create_directories(devnode.parent_path());
  if (::mknod(devnode.c_str(), S_IFCHR | 0666, makedev(maj, min)) != 0 && errno != EEXIST)
    return "ERR mknod failed: " + std::string(strerror(errno)) + "\n";
  chmod(devnode.c_str(), 0666);

  std::string devpath = sysfs_path.substr(4);
  fire_udev("add", devname, devpath, maj, min, seqnum++);
  tracked_devices[sysfs_path] = devname;
  log("created /dev/" + devname + " (" + std::to_string(maj) + ":" + std::to_string(min) + ")");
  return "OK " + devname + "\n";
}

static std::string handle_remove(const std::string &sysfs_path) {
  static int seqnum = 5000;
  auto it = tracked_devices.find(sysfs_path);
  if (it == tracked_devices.end()) return "ERR device not tracked\n";

  std::string devname = it->second;
  unlink((fs::path("/dev") / devname).c_str());
  fire_udev("remove", devname, sysfs_path.substr(4), INPUT_MAJOR, 0, seqnum++);
  log("removed /dev/" + devname);
  tracked_devices.erase(it);
  return "OK\n";
}

static void cleanup_all() {
  for (auto &[path, devname] : tracked_devices) {
    unlink((fs::path("/dev") / devname).c_str());
    log("cleanup /dev/" + devname);
  }
  tracked_devices.clear();
  if (listen_fd >= 0) close(listen_fd);
  unlink(SOCK_PATH);
}

static void signal_handler(int) { running = 0; }

static void handle_client(int fd) {
  FILE *fp = fdopen(fd, "r+");
  if (!fp) { close(fd); return; }

  char buf[4096];
  while (fgets(buf, sizeof(buf), fp)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

    std::string resp;
    if (line.rfind("MKNOD ", 0) == 0)       resp = handle_mknod(line.substr(6));
    else if (line.rfind("REMOVE ", 0) == 0)  resp = handle_remove(line.substr(7));
    else                                      resp = "ERR unknown command\n";

    fputs(resp.c_str(), fp);
    fflush(fp);
  }
  fclose(fp);
}
int main() {
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGCHLD, SIG_IGN);

  unlink(SOCK_PATH);
  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) { log("socket: " + std::string(strerror(errno))); return 1; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

  mode_t old_umask = umask(0);
  if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    log("bind: " + std::string(strerror(errno)));
    umask(old_umask);
    return 1;
  }
  umask(old_umask);

  if (listen(listen_fd, 4) < 0) { log("listen: " + std::string(strerror(errno))); return 1; }
  log("listening on " + std::string(SOCK_PATH));

  while (running) {
    int client_fd = accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (errno == EINTR) continue;
      log("accept: " + std::string(strerror(errno)));
      break;
    }
    handle_client(client_fd);
  }

  cleanup_all();
  log("shutdown");
  return 0;
}
