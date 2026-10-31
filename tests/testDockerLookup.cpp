#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <core/docker.hpp>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {
class DockerLookupServer {
  int fd;
  std::atomic_bool stopping{false};
  std::thread worker;

public:
  const std::string path = "/tmp/wolf-docker-lookup-" + std::to_string(getpid()) + ".sock";

  explicit DockerLookupServer(bool all_gone) : fd(socket(AF_UNIX, SOCK_STREAM, 0)) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    unlink(path.c_str());
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(fd, 8)) {
      throw std::runtime_error("Unable to start fake Docker socket");
    }
    worker = std::thread([this, all_gone] {
      while (!stopping) {
        pollfd ready{fd, POLLIN, 0};
        if (poll(&ready, 1, 50) <= 0) {
          continue;
        }
        int client = accept(fd, nullptr, nullptr);
        if (client < 0) {
          continue;
        }
        timeval timeout{1, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::string request;
        char buf[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
          auto n = recv(client, buf, sizeof(buf), 0);
          if (n <= 0) {
            break;
          }
          request.append(buf, n);
        }
        int status = 200;
        std::string body;
        if (request.find("GET /version ") == 0) {
          body = R"({"ApiVersion":"1.41"})";
        } else if (request.find("GET /v1.41/containers/json") == 0) {
          body = R"([{"Id":"removed"},{"Id":"alive"}])";
        } else if (request.find("GET /v1.41/containers/alive/json ") == 0 && !all_gone) {
          body =
              R"({"Id":"alive","Name":"/alive","State":{"Status":"running"},"HostConfig":{"PortBindings":null,"Devices":null},"Mounts":null,"Config":{"Env":null,"Hostname":"alive","Image":"test-image"}})";
        } else {
          status = 404;
          body = R"({"message":"No such container"})";
        }
        const std::string response =
            "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK\r\n" : " Not Found\r\n") +
            "Connection: close\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\n\r\n" + body;
        send(client, response.data(), response.size(), MSG_NOSIGNAL);
        close(client);
      }
    });
  }

  ~DockerLookupServer() {
    stopping = true;
    worker.join();
    close(fd);
    unlink(path.c_str());
  }
};
} // namespace

TEST_CASE("Docker list tolerates containers removed before inspection", "[DockerLookup]") {
  const bool all_gone = GENERATE(false, true);
  DockerLookupServer server(all_gone);
  wolf::core::docker::DockerAPI api(server.path);
  std::vector<wolf::core::docker::Container> containers;
  REQUIRE_NOTHROW(containers = api.get_containers());
  REQUIRE(containers.size() == (all_gone ? 0 : 1));
  if (!all_gone) {
    REQUIRE(containers.front().id == "alive");
    REQUIRE(containers.front().status == wolf::core::docker::RUNNING);
  }
}
