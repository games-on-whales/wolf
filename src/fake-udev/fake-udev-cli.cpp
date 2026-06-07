#include <fake-udev/fake-udev.hpp>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

constexpr int UDEV_EVENT_MODE = 2;

/**
 * Broadcasting the uevent is not enough on its own: libudev consumers (e.g. SDL)
 * enumerate devices from the on-disk udev database, not from the netlink event.
 * In containers without a running udevd nothing writes that database, so a device
 * we advertise is never tagged (ID_INPUT_JOYSTICK, etc.) and applications ignore
 * it. Persist the same record systemd-udevd would, keyed on MAJOR:MINOR.
 */
static void persist_udev_db(const std::string &msg) {
  std::string action, major, minor, tags, usec;
  std::vector<std::pair<std::string, std::string>> props;
  for (size_t start = 0; start < msg.size();) {
    size_t end = msg.find('\0', start);
    if (end == std::string::npos)
      end = msg.size();
    const std::string tok = msg.substr(start, end - start);
    start = end + 1;
    const size_t eq = tok.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
    if (k == "ACTION")
      action = v;
    else if (k == "MAJOR")
      major = v;
    else if (k == "MINOR")
      minor = v;
    else if ((k == "TAGS" || k == "CURRENT_TAGS") && tags.empty())
      tags = v;
    else if (k == "USEC_INITIALIZED")
      usec = v;
    else if (!k.empty() && k[0] != '.' && k != "DEVPATH" && k != "SUBSYSTEM" && k != "DEVNAME" && k != "DEVTYPE" &&
             k != "SEQNUM" && k != "DRIVER" && k != "MODALIAS" && k != "SYNTH_UUID" && k != "DEVLINKS")
      props.emplace_back(k, v);
  }

  // Input devices are character devices; the db key mirrors udev's c<maj>:<min>.
  if (major.empty() || minor.empty())
    return;
  const std::string path = "/run/udev/data/c" + major + ":" + minor;

  if (action == "remove") {
    ::unlink(path.c_str());
    return;
  }

  ::mkdir("/run/udev", 0755);
  ::mkdir("/run/udev/data", 0755);
  std::ofstream db(path, std::ios::trunc);
  if (!db) {
    std::cout << "Could not write udev db entry " << path << ": " << strerror(errno) << std::endl;
    return;
  }
  if (!usec.empty())
    db << "I:" << usec << "\n";
  for (const auto &p : props)
    db << "E:" << p.first << "=" << p.second << "\n";
  // TAGS is formatted ":a:b:"; udev records every tag as G: and each sticky tag as Q:.
  for (size_t i = 0; i < tags.size();) {
    if (tags[i] == ':') {
      ++i;
      continue;
    }
    size_t j = tags.find(':', i);
    if (j == std::string::npos)
      j = tags.size();
    const std::string tag = tags.substr(i, j - i);
    db << "G:" << tag << "\n"
       << "Q:" << tag << "\n";
    i = j + 1;
  }
  db << "V:1\n";
}

int main(int argc, char *argv[]) {
  InputParser input(argc, argv);
  int rc = -1;

  if (input.cmdOptionExists("-h") || input.cmdOptionExists("--help")) {
    std::cout << "Usage: fake-udev -m <base64 encoded message> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help" << std::endl;
    std::cout << "  -m <base64 encoded message>" << std::endl;
    std::cout << "  --sock-domain <domain>        | default: AF_NETLINK" << std::endl;
    std::cout << "  --sock-type <type>            | default: SOCK_RAW" << std::endl;
    std::cout << "  --sock-protocol <protocol>    | default: NETLINK_KOBJECT_UEVENT" << std::endl;
    std::cout << "  --sock-groups <groups>        | default: UDEV_EVENT_MODE" << std::endl;
    std::cout << "  --udev-subsystem <subsystem>  | default: input" << std::endl;
    std::cout << "  --udev-devtype <devtype>      | default: " << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout
        << "echo -ne \"ACTION=add\\0DEVNAME=input/bomb\\0DEVPATH=/devices/bomb\\0SEQNUM=1234\\0SUBSYSTEM=input\\0\" | "
           "base64 | sudo fake-udev"
        << std::endl;
    std::cout << " `udevadm monitor` should print something like:" << std::endl;
    std::cout << "UDEV  [3931.403835] add      /devices/bomb  (input)" << std::endl;
    return 0;
  }

  auto msg = input.getCmdOption("-m");
  if (msg.empty()) {
    std::string in;
    while (std::cin >> in) {
      msg.append(in);
    }
  }
  if (!msg.empty()) {
    msg = base64_decode(msg);
    std::cout << "Sending " << msg << std::endl;

    // Persist the udev database entry before broadcasting, so a consumer that
    // looks the device up in response to the event finds a complete record.
    persist_udev_db(msg);

    int domain = input.getCmdOption("--sock-domain", AF_NETLINK);
    int type = input.getCmdOption("--sock-type", SOCK_RAW);
    int protocol = input.getCmdOption("--sock-protocol", NETLINK_KOBJECT_UEVENT);
    unsigned int groups = input.getCmdOption("--sock-groups", UDEV_EVENT_MODE);

    auto udev_subsystem = input.getCmdOption("--udev-subsystem", "input");
    auto udev_devtype = input.getCmdOption("--udev-devtype", "");

    netlink_connection conn{};
    if (connect(conn, domain, type, protocol, groups)) {
      auto header = make_udev_header(msg, udev_subsystem, udev_devtype);
      if (send_msgs(conn, {header, msg})) {
        std::cout << "Message sent" << std::endl;
        rc = 0;
      }
    }

    cleanup(conn);
  } else {
    std::cout << "No messages to send, have you forgot to pass -m ?" << std::endl;
  }

  return rc;
}
