#include <fmt/format.h>

#include <iostream>
#include <tuple>
#include <vector>

#include <pipewire/pipewire.h>

#include <log/logger.cpp>
#include <rest/servers.cpp>

int main(int argc, char *argv[]) {
  Logger::init(debug);
  Logger::log(debug,
              "Compiled with libpipewire {} - Linked with libpipewire: {}\n",
              pw_get_headers_version(),
              pw_get_library_version());

  auto https_server = HTTPServers::createHTTPS("key.pem", "cert.pem");
  auto http_server = HTTPServers::createHTTP();

  auto https_thread = HTTPServers::startServer(https_server.get(), 8080);
  auto http_thread = HTTPServers::startServer(http_server.get(), 8081);

  https_thread.join();
  http_thread.join();
  return 0;
}