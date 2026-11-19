#include <algorithm>
#include <boost/core/null_deleter.hpp>
#include <boost/json.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <iterator>
#include <platforms/mount.hpp>
#include <runners/rootless.hpp>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#endif

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using namespace wolf::core::docker;
namespace json = boost::json;

namespace {

// Removes a temp directory when the case ends, including on a failed REQUIRE.
struct TempDirGuard {
  std::filesystem::path path;
  explicit TempDirGuard(std::filesystem::path p) : path(std::move(p)) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  ~TempDirGuard() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

class ScopedEnv {
public:
  ScopedEnv(const char *name, const char *value) : name(name) {
    if (auto old = ::getenv(name)) {
      previous = old;
    }
    if (value) {
      ::setenv(name, value, 1);
    } else {
      ::unsetenv(name);
    }
  }

  ~ScopedEnv() {
    if (previous) {
      ::setenv(name, previous->c_str(), 1);
    } else {
      ::unsetenv(name);
    }
  }

private:
  const char *name;
  std::optional<std::string> previous;
};

std::string str(const json::value &value) {
  return std::string(value.as_string());
}

json::object host_config_of(const std::string &create_json) {
  return json::parse(create_json).as_object().at("HostConfig").as_object();
}

// Collects what Wolf logs while it is alive, one "LEVEL message" line per record.
class LogCapture {
  using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;

public:
  LogCapture() {
    auto backend = boost::make_shared<boost::log::sinks::text_ostream_backend>();
    backend->add_stream(boost::shared_ptr<std::ostream>(&out, boost::null_deleter()));
    backend->auto_flush(true);
    sink = boost::make_shared<Sink>(backend);
    sink->set_formatter([](boost::log::record_view const &rec, boost::log::formatting_ostream &strm) {
      auto severity = rec[boost::log::trivial::severity];
      auto msg = rec[boost::log::expressions::smessage];
      strm << logs::get_name(severity.get()) << " " << msg.get();
    });
    boost::log::core::get()->add_sink(sink);
  }

  ~LogCapture() {
    boost::log::core::get()->remove_sink(sink);
  }

  std::string text() const {
    return out.str();
  }

private:
  std::ostringstream out;
  boost::shared_ptr<Sink> sink;
};

} // namespace

TEST_CASE("is_rootless", "[ROOTLESS]") {
  SECTION("unset") {
    ScopedEnv env("WOLF_ROOTLESS", nullptr);
    REQUIRE_FALSE(is_rootless());
  }
  SECTION("TRUE") {
    ScopedEnv env("WOLF_ROOTLESS", "TRUE");
    REQUIRE(is_rootless());
  }
  SECTION("FALSE") {
    ScopedEnv env("WOLF_ROOTLESS", "FALSE");
    REQUIRE_FALSE(is_rootless());
  }
  SECTION("1") {
    ScopedEnv env("WOLF_ROOTLESS", "1");
    REQUIRE_FALSE(is_rootless());
  }
}

TEST_CASE("apply_rootless_hostconfig", "[ROOTLESS]") {
  SECTION("erases DeviceCgroupRules") {
    auto out = apply_rootless_hostconfig(R"({"HostConfig":{"DeviceCgroupRules":["c 13:* rmw"],"IpcMode":"host"}})", 0);
    REQUIRE_FALSE(host_config_of(out).contains("DeviceCgroupRules"));
    REQUIRE(str(host_config_of(out).at("IpcMode")) == "host");
  }

  SECTION("erases a null DeviceCgroupRules") {
    auto out = apply_rootless_hostconfig(R"({"HostConfig":{"DeviceCgroupRules":null}})", 0);
    REQUIRE_FALSE(host_config_of(out).contains("DeviceCgroupRules"));
  }

  SECTION("creates HostConfig when missing") {
    auto out = apply_rootless_hostconfig(R"({"Image":"x"})", 0);
    auto host_config = host_config_of(out);
    REQUIRE(host_config.at("SecurityOpt").as_array().size() == 1);
    REQUIRE(str(host_config.at("SecurityOpt").as_array()[0]) == "label=disable");
    REQUIRE(str(json::parse(out).as_object().at("Image")) == "x");
  }

  SECTION("replaces a null HostConfig") {
    auto out = apply_rootless_hostconfig(R"({"Image":"x","HostConfig":null})", 50);
    auto host_config = host_config_of(out);
    REQUIRE(host_config.at("SecurityOpt").as_array().size() == 1);
    REQUIRE(str(host_config.at("SecurityOpt").as_array()[0]) == "label=disable");
    REQUIRE(host_config.at("OomScoreAdj").as_int64() == 50);
    REQUIRE(str(json::parse(out).as_object().at("Image")) == "x");
  }

  SECTION("adds label=disable once") {
    auto once = apply_rootless_hostconfig(R"({"HostConfig":{"SecurityOpt":["seccomp=unconfined"]}})", 0);
    auto twice = apply_rootless_hostconfig(once, 0);
    auto opts = host_config_of(twice).at("SecurityOpt").as_array();
    REQUIRE(opts.size() == 2);
    REQUIRE(str(opts[0]) == "seccomp=unconfined");
    REQUIRE(str(opts[1]) == "label=disable");
  }

  SECTION("skips label=disable when the profile sets a label") {
    auto out = apply_rootless_hostconfig(R"({"HostConfig":{"SecurityOpt":["label=type:container_t"]}})", 0);
    auto opts = host_config_of(out).at("SecurityOpt").as_array();
    REQUIRE(opts.size() == 1);
    REQUIRE(str(opts[0]) == "label=type:container_t");
  }

  SECTION("leaves everything else alone") {
    auto in = R"({"HostConfig":{"IpcMode":"host","Runtime":"nvidia","CapAdd":["NET_ADMIN"],)"
              R"("DeviceRequests":[{"Driver":"cdi"}]}})";
    auto before = host_config_of(in);
    auto after = host_config_of(apply_rootless_hostconfig(in, 0));
    for (const auto &key : {"IpcMode", "Runtime", "CapAdd", "DeviceRequests"}) {
      REQUIRE(after.at(key) == before.at(key));
    }
    REQUIRE(after.size() == before.size() + 2);
  }

  SECTION("only adds to the default options") {
    auto out = host_config_of(apply_rootless_hostconfig(R"({"HostConfig":{"IpcMode":"host"}})", 0));
    REQUIRE(out.size() == 3);
    REQUIRE(str(out.at("IpcMode")) == "host");
    REQUIRE(out.at("SecurityOpt").as_array().size() == 1);
    REQUIRE(str(out.at("SecurityOpt").as_array()[0]) == "label=disable");
    REQUIRE(out.at("OomScoreAdj").as_int64() == 0);
  }

  SECTION("sets OomScoreAdj when the profile has none") {
    auto out = apply_rootless_hostconfig(R"({"HostConfig":{"IpcMode":"host"}})", 200);
    REQUIRE(host_config_of(out).at("OomScoreAdj").as_int64() == 200);
  }

  SECTION("sets OomScoreAdj when HostConfig is missing") {
    auto out = apply_rootless_hostconfig(R"({"Image":"x"})", 100);
    REQUIRE(host_config_of(out).at("OomScoreAdj").as_int64() == 100);
  }

  SECTION("sets OomScoreAdj when the profile has null") {
    auto out = apply_rootless_hostconfig(R"({"HostConfig":{"OomScoreAdj":null}})", 200);
    REQUIRE(host_config_of(out).at("OomScoreAdj").as_int64() == 200);
  }

  SECTION("keeps a profile set OomScoreAdj") {
    auto out = apply_rootless_hostconfig(R"({"HostConfig":{"OomScoreAdj":-500}})", 200);
    REQUIRE(host_config_of(out).at("OomScoreAdj").as_int64() == -500);
    auto zero = apply_rootless_hostconfig(R"({"HostConfig":{"OomScoreAdj":0}})", 200);
    REQUIRE(host_config_of(zero).at("OomScoreAdj").as_int64() == 0);
  }
}

TEST_CASE("own_oom_score_adj", "[ROOTLESS]") {
#if defined(__linux__)
  {
    int own = 0;
    std::ifstream("/proc/self/oom_score_adj") >> own;
    REQUIRE(own_oom_score_adj() == std::max(own, 0));
  }
#else
  REQUIRE(own_oom_score_adj() >= 0);
#endif

#if defined(__linux__)
  // A child changes its own score, so this process keeps its value.
  auto score_in_child = [](const char *write_value) {
    auto pid = ::fork();
    if (pid == 0) {
      std::ofstream out("/proc/self/oom_score_adj");
      out << write_value << std::flush;
      ::_exit(out ? own_oom_score_adj() : -1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
  };

  SECTION("reads the score") {
    auto score = score_in_child("200");
    if (score == 255) {
      SKIP("writing oom_score_adj is not permitted here");
    }
    REQUIRE(score == 200);
  }

  SECTION("never below 0") {
    auto score = score_in_child("-100");
    if (score == 255) {
      SKIP("lowering oom_score_adj needs CAP_SYS_RESOURCE");
    }
    REQUIRE(score == 0);
  }
#endif
}

TEST_CASE("prepare_input_dir clears what a previous run left", "[ROOTLESS]") {
  TempDirGuard guard(std::filesystem::temp_directory_path() / fmt::format("wolf-rootless-prep-{}", ::getpid()));
  const auto &dir = guard.path;
  auto input_dir = dir / "input";

  SECTION("creates a missing directory") {
    prepare_input_dir(input_dir);
    REQUIRE(std::filesystem::is_directory(input_dir));
    REQUIRE(std::filesystem::is_empty(input_dir));
  }

  SECTION("warns when the directory cannot be created") {
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "file") << "not a directory";
    LogCapture capture;
    prepare_input_dir(dir / "file" / "input");
    REQUIRE_FALSE(std::filesystem::exists(dir / "file" / "input"));
    REQUIRE_THAT(capture.text(), ContainsSubstring("WARN [DOCKER] Unable to create"));
  }

  SECTION("removes stale entries") {
    std::filesystem::create_directories(input_dir / "sub");
    std::ofstream(input_dir / "event7") << "stale";
    std::ofstream(input_dir / "sub" / "hidraw3") << "stale";
    prepare_input_dir(input_dir);
    REQUIRE(std::filesystem::is_directory(input_dir));
    REQUIRE(std::filesystem::is_empty(input_dir));
  }
}

TEST_CASE("plan_input_bind", "[ROOTLESS]") {
  std::filesystem::path dir = "/etc/wolf/state/input";

  SECTION("input node") {
    auto plan = plan_input_bind(dir, "/dev/input/event7");
    REQUIRE(plan);
    REQUIRE(plan->source == "/dev/input/event7");
    REQUIRE(plan->target == "/etc/wolf/state/input/event7");
    REQUIRE(plan->link.empty());
    REQUIRE_THAT(plug_command("MSG", *plan), Equals("fake-udev -m MSG"));
    REQUIRE_THAT(unplug_command("MSG", *plan), Equals("fake-udev -m MSG"));
  }

  SECTION("hidraw node") {
    auto plan = plan_input_bind(dir, "/dev/hidraw3");
    REQUIRE(plan);
    REQUIRE(plan->source == "/dev/hidraw3");
    REQUIRE(plan->target == "/etc/wolf/state/input/hidraw3");
    REQUIRE(plan->link == "/dev/hidraw3");
    REQUIRE_THAT(plug_command("MSG", *plan), Equals("ln -sf /dev/input/hidraw3 /dev/hidraw3 && fake-udev -m MSG"));
    REQUIRE_THAT(unplug_command("MSG", *plan), Equals("fake-udev -m MSG && rm -f /dev/hidraw3"));
  }

  SECTION("rejected") {
    REQUIRE_FALSE(plan_input_bind(dir, "/dev/input/../../etc/passwd"));
    REQUIRE_FALSE(plan_input_bind(dir, "/dev/input/.."));
    REQUIRE_FALSE(plan_input_bind(dir, "/dev/hidraw3/../../etc/passwd"));
    REQUIRE_FALSE(plan_input_bind(dir, "/etc/passwd"));
    REQUIRE_FALSE(plan_input_bind(dir, "/dev/sda"));
    REQUIRE_FALSE(plan_input_bind(dir, "/dev/input/"));
    REQUIRE_FALSE(plan_input_bind(dir, "/dev/input/sub/event1"));
    REQUIRE_FALSE(plan_input_bind(dir, "input/event1"));
    REQUIRE_FALSE(plan_input_bind(dir, ""));
  }
}

TEST_CASE("InputBinds plug failures", "[ROOTLESS]") {
  TempDirGuard guard(std::filesystem::temp_directory_path() / fmt::format("wolf-rootless-plug-{}", ::getpid()));
  const auto &dir = guard.path;
  std::filesystem::create_directories(dir / "input");
  InputBinds binds;

  SECTION("warns when the target cannot be created") {
    auto plan = InputBindPlan{.source = dir / "source", .target = dir / "missing" / "event7", .link = {}};
    LogCapture capture;
    REQUIRE_FALSE(binds.plug(plan));
    REQUIRE_THAT(capture.text(), ContainsSubstring("WARN [DOCKER] Unable to create"));
    REQUIRE_FALSE(std::filesystem::exists(dir / "missing"));
  }

  SECTION("warns when the bind fails") {
    // a directory cannot be bound over a file, and an unprivileged mount is refused: neither is a vanished node
    std::filesystem::create_directories(dir / "source");
    auto plan = InputBindPlan{.source = dir / "source", .target = dir / "input" / "event7", .link = {}};
    LogCapture capture;
    REQUIRE_FALSE(binds.plug(plan));
    REQUIRE_THAT(capture.text(), ContainsSubstring("WARN [DOCKER] Unable to bind"));
    REQUIRE_FALSE(std::filesystem::exists(plan.target));
  }
}

#if defined(__linux__)
namespace {

// Gives the (forked) calling process a fresh user and mount namespace where it is root, false when not available.
bool enter_userns() {
  auto uid = ::getuid();
  auto gid = ::getgid();
  if (::unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
    return false;
  }
  auto write_file = [](const char *path, const std::string &content) {
    int fd = ::open(path, O_WRONLY);
    if (fd < 0) {
      return false;
    }
    bool ok = ::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
    ::close(fd);
    return ok;
  };
  auto uid_map = "0 " + std::to_string(uid) + " 1";
  auto gid_map = "0 " + std::to_string(gid) + " 1";
  if (!write_file("/proc/self/setgroups", "deny") || !write_file("/proc/self/uid_map", uid_map) ||
      !write_file("/proc/self/gid_map", gid_map)) {
    return false;
  }
  return true;
}

/**
 * Runs in a forked child that owns a fresh user and mount namespace: returns 0 on success,
 * 77 when the namespaces are not available, anything else is the failed step.
 */
int bind_and_detach_in_userns(const std::string &source, const std::string &target) {
  if (!enter_userns()) {
    return 77;
  }

  auto read_all = [](const std::string &path) {
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), {});
  };

  if (auto ec = bind_node(source, target)) {
    return ec == std::errc::operation_not_permitted || ec == std::errc::permission_denied ? 77 : 1;
  }
  if (read_all(target) != "bound") {
    return 2;
  }

  // a replug stacks a second bind, both come off one at a time
  if (bind_node(source, target)) {
    return 3;
  }
  if (unbind_node(target) || read_all(target) != "bound") {
    return 4;
  }

  int held = ::open(target.c_str(), O_RDONLY);
  if (held < 0) {
    return 5;
  }
  if (unbind_node(target)) {
    return 6;
  }
  char buf[8] = {};
  if (::pread(held, buf, sizeof(buf), 0) != 5) {
    return 7;
  }
  ::close(held);
  if (!read_all(target).empty()) {
    return 8;
  }
  if (!unbind_node(target)) {
    return 9;
  }

  auto input_dir = std::filesystem::path(target).parent_path() / "input";
  auto plan = InputBindPlan{.source = source, .target = input_dir / "event7", .link = {}};

  prepare_input_dir(input_dir);
  InputBinds binds;
  if (!binds.plug(plan) || !binds.plug(plan) || read_all(plan.target) != "bound") {
    return 10;
  }
  binds.unplug(plan);
  if (std::filesystem::exists(plan.target)) {
    return 11;
  }

  if (!binds.plug(plan)) {
    return 12;
  }
  binds.release_all();
  if (std::filesystem::exists(plan.target)) {
    return 13;
  }

  InputBinds orphaned;
  if (!orphaned.plug(plan)) {
    return 14;
  }
  prepare_input_dir(input_dir);
  if (!std::filesystem::is_directory(input_dir) || !std::filesystem::is_empty(input_dir)) {
    return 15;
  }

  // a node that vanished before it was bound is logged below warning level and leaves nothing behind
  auto vanished = InputBindPlan{.source = input_dir / "gone", .target = input_dir / "event8", .link = {}};
  LogCapture capture;
  if (binds.plug(vanished) || std::filesystem::exists(vanished.target)) {
    return 16;
  }
  if (capture.text().find("WARN") != std::string::npos) {
    return 17;
  }
  return 0;
}

/**
 * Runs in a forked child whose /proc is replaced by an empty tmpfs: `content` (when given) is the only thing
 * /proc/self/oom_score_adj holds. Returns what own_oom_score_adj makes of it, 77 when /proc cannot be replaced.
 */
int own_oom_score_adj_with_proc(const std::optional<std::string> &content) {
  if (!enter_userns() || ::mount("tmpfs", "/proc", "tmpfs", 0, nullptr) != 0) {
    return 77;
  }
  if (content) {
    std::filesystem::create_directories("/proc/self");
    std::ofstream("/proc/self/oom_score_adj") << *content;
  }
  return own_oom_score_adj();
}

} // namespace

TEST_CASE("own_oom_score_adj falls back to 0 when the score cannot be read", "[ROOTLESS]") {
  auto score_with = [](const std::optional<std::string> &content) {
    auto pid = ::fork();
    if (pid == 0) {
      ::_exit(own_oom_score_adj_with_proc(content));
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
  };

  SECTION("no file") {
    auto score = score_with(std::nullopt);
    if (score == 77) {
      SKIP("cannot replace /proc here");
    }
    REQUIRE(score == 0);
  }

  SECTION("empty") {
    auto score = score_with("");
    if (score == 77) {
      SKIP("cannot replace /proc here");
    }
    REQUIRE(score == 0);
  }

  SECTION("not a number") {
    auto score = score_with("unreadable\n");
    if (score == 77) {
      SKIP("cannot replace /proc here");
    }
    REQUIRE(score == 0);
  }

  SECTION("a number is still read") {
    auto score = score_with("150\n");
    if (score == 77) {
      SKIP("cannot replace /proc here");
    }
    REQUIRE(score == 150);
  }
}

TEST_CASE("bind and lazily detach a node", "[ROOTLESS]") {
  TempDirGuard guard(std::filesystem::temp_directory_path() / fmt::format("wolf-rootless-{}", ::getpid()));
  const auto &dir = guard.path;
  std::filesystem::create_directories(dir);
  auto source = (dir / "source").string();
  auto target = (dir / "target").string();
  std::ofstream(source) << "bound";
  std::ofstream(target).close();

  auto pid = ::fork();
  REQUIRE(pid >= 0);
  if (pid == 0) {
    ::_exit(bind_and_detach_in_userns(source, target));
  }
  int status = 0;
  ::waitpid(pid, &status, 0);

  REQUIRE(WIFEXITED(status));
  // 77 means the namespaces are unavailable, any other non-zero value is the failed step in bind_and_detach_in_userns
  INFO("bind_and_detach_in_userns step result");
  CAPTURE(WEXITSTATUS(status));
  if (WEXITSTATUS(status) == 77) {
    SKIP("unprivileged user namespaces are not available");
  }
  REQUIRE(WEXITSTATUS(status) == 0);
}
#endif
