#include <boost/asio/ip/tcp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <runners/process.hpp>
#include <sstream>
#include <sys/stat.h>

using Catch::Matchers::ContainsSubstring;

TEST_CASE("Process runner doesn't leak Wolf's sockets to the child", "[PROCESS]") {
  // Stand-in for Wolf's HTTP/RTSP listeners, which are not opened with close-on-exec
  boost::asio::io_context io;
  boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::tcp::v4(), 0});
  struct stat sock_stat {};
  REQUIRE(fstat(acceptor.native_handle(), &sock_stat) == 0);
  auto listen_socket = fmt::format("socket:[{}]", sock_stat.st_ino);

  auto out_file = std::filesystem::temp_directory_path() / fmt::format("wolf-process-fds-{}", getpid());
  std::filesystem::remove(out_file);

  auto runner = process::RunProcess(std::make_shared<wolf::core::events::EventBusType>(),
                                    fmt::format("/bin/sh -c \"ls -l /proc/$$/fd > {}\"", out_file.string()));
  runner.run("1", "", "", std::make_shared<wolf::core::events::devices_atom_queue>(), {}, {}, {}, "");

  std::ifstream in(out_file);
  std::stringstream child_fds;
  child_fds << in.rdbuf();
  std::filesystem::remove(out_file);

  REQUIRE_THAT(child_fds.str(), ContainsSubstring(" 2 -> ")); // the child ran, and kept its stderr
  REQUIRE_THAT(child_fds.str(), !ContainsSubstring(listen_socket));
}
