#include <fmt/format.h>

#include <iostream>
#include <tuple>
#include <vector>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <pipewire/pipewire.h>

int main(int argc, char *argv[]) {
  fmt::print("Compiled with libpipewire {}\nLinked with libpipewire: {}\n",
             pw_get_headers_version(),
             pw_get_library_version());
  return 0;
}