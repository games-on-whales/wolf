#include <fake-udev/fake-udev.hpp>

constexpr int UDEV_EVENT_MODE = 2;
int main(int argc, char *argv[]) {
  InputParser input(argc, argv);

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

    int domain = input.getCmdOption("--sock-domain", AF_NETLINK);
    int type = input.getCmdOption("--sock-type", SOCK_RAW);
    int protocol = input.getCmdOption("--sock-protocol", NETLINK_KOBJECT_UEVENT);
    unsigned int groups = input.getCmdOption("--sock-groups", UDEV_EVENT_MODE);

    auto udev_subsystem = input.getCmdOption("--udev-subsystem", "input");
    auto udev_devtype = input.getCmdOption("--udev-devtype", "");

    netlink_connection conn{};
    if (connect(conn, domain, type, protocol, groups)) {
      auto header = make_udev_header(msg, udev_subsystem, udev_devtype);
      if (send_msgs(conn, {header, msg}))
        std::cout << "Message sent" << std::endl;
    }

    cleanup(conn);
    return 0;
  } else {
    std::cout << "No messages to send, have you forgot to pass -m ?" << std::endl;
  }

  return -1;
}