#include <catch2/catch_test_macros.hpp>
#include <events/events.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <rfl/toml.hpp>
#include <state/config.hpp>

namespace {

class TemporaryConfig {
public:
  TemporaryConfig()
      : directory_(std::filesystem::temp_directory_path() / ("wolf-config-test-" + state::gen_uuid())),
        path_(directory_ / "config.toml") {
    std::filesystem::create_directories(directory_);
    std::filesystem::copy_file("config.test.toml", path_);
  }

  ~TemporaryConfig() {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  const std::filesystem::path &path() const {
    return path_;
  }
  const std::filesystem::path &directory() const {
    return directory_;
  }

private:
  std::filesystem::path directory_;
  std::filesystem::path path_;
};

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
}

state::Config load_config(const std::filesystem::path &path) {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  return state::load_or_default(path.string(), event_bus, running_sessions);
}

std::vector<std::filesystem::path> matching_files(const std::filesystem::path &directory, const std::string &needle) {
  std::vector<std::filesystem::path> matches;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().filename().string().find(needle) != std::string::npos) {
      matches.push_back(entry.path());
    }
  }
  return matches;
}

constexpr std::string_view stale_tail_corruption = R"(config_version = 10
hostname = 'wolf'
uuid = 'test'

[gstreamer.video]
hevc_encoders = []

[[gstreamer.video.hevc_encoders]]
plugin_name = 'x265'
)";

} // namespace

TEST_CASE("shorter config updates truncate the previous document", "[CONFIG][PERSISTENCE]") {
  TemporaryConfig temporary;
  auto config = load_config(temporary.path());
  const auto original = read_file(temporary.path());
  const auto original_size = std::filesystem::file_size(temporary.path());

  state::update_profiles(config, {});

  REQUIRE(std::filesystem::file_size(temporary.path()) < original_size);
  REQUIRE_NOTHROW(rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(temporary.path().string()).value());
  REQUIRE(read_file(temporary.path().string() + ".last-good") == original);
  REQUIRE(matching_files(temporary.directory(), ".tmp-").empty());
}

TEST_CASE("config updates preserve source permissions", "[CONFIG][PERSISTENCE]") {
  TemporaryConfig temporary;
  std::filesystem::permissions(
      temporary.path(),
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read);
  const auto expected = std::filesystem::status(temporary.path()).permissions();
  auto config = load_config(temporary.path());

  state::update_profiles(config, {});

  REQUIRE(std::filesystem::status(temporary.path()).permissions() == expected);
}

TEST_CASE("pairing mutations remain valid when the serialized document shrinks", "[CONFIG][PERSISTENCE]") {
  TemporaryConfig temporary;
  auto config = load_config(temporary.path());
  const state::PairedClient client{.client_cert = "persistence-test-client",
                                   .app_state_folder = "a-long-folder-name",
                                   .settings = {}};

  state::pair(config, client);
  state::unpair(config, client);

  auto persisted = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(temporary.path().string()).value();
  REQUIRE(std::none_of(persisted.paired_clients.begin(), persisted.paired_clients.end(), [](const auto &candidate) {
    return candidate.client_cert == "persistence-test-client";
  }));
  REQUIRE(matching_files(temporary.directory(), ".tmp-").empty());
}

TEST_CASE("concurrent config mutations do not lose persisted clients", "[CONFIG][PERSISTENCE]") {
  TemporaryConfig temporary;
  auto config = load_config(temporary.path());
  const auto original_count =
      rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(temporary.path().string())
          .value()
          .paired_clients.size();
  std::vector<std::future<void>> mutations;

  for (int id = 0; id < 8; ++id) {
    mutations.emplace_back(std::async(std::launch::async, [&, id] {
      state::pair(config,
                  state::PairedClient{.client_cert = "concurrent-client-" + std::to_string(id),
                                      .app_state_folder = "folder-" + std::to_string(id),
                                      .settings = {}});
    }));
  }
  for (auto &mutation : mutations) {
    mutation.get();
  }

  auto persisted = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(temporary.path().string()).value();
  REQUIRE(persisted.paired_clients.size() == original_count + mutations.size());
  REQUIRE(matching_files(temporary.directory(), ".tmp-").empty());
}

TEST_CASE("known stale-tail corruption restores the last-known-good config", "[CONFIG][RECOVERY]") {
  TemporaryConfig temporary;
  const auto original = read_file(temporary.path());
  std::filesystem::copy_file(temporary.path(), temporary.path().string() + ".last-good");
  write_file(temporary.path(), std::string(stale_tail_corruption));

  REQUIRE_NOTHROW(load_config(temporary.path()));
  REQUIRE(read_file(temporary.path()) == original);
  REQUIRE(matching_files(temporary.directory(), ".corrupt-").size() == 1);
  REQUIRE(read_file(matching_files(temporary.directory(), ".corrupt-").front()) == stale_tail_corruption);
}

TEST_CASE("known stale-tail corruption self-heals without a backup", "[CONFIG][RECOVERY]") {
  TemporaryConfig temporary;
  write_file(temporary.path(), std::string(stale_tail_corruption));

  // A headless test worker may reject the recovered defaults later during
  // hardware encoder selection. Recovery itself is complete before that gate.
  try {
    load_config(temporary.path());
  } catch (const std::runtime_error &) {
  }
  REQUIRE_NOTHROW(rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(temporary.path().string()).value());
  REQUIRE(matching_files(temporary.directory(), ".corrupt-").size() == 1);
}

TEST_CASE("unrecognized malformed configs fail closed without being replaced", "[CONFIG][RECOVERY]") {
  TemporaryConfig temporary;
  const std::string malformed = "this is not = [valid TOML";
  write_file(temporary.path(), malformed);

  REQUIRE_THROWS(load_config(temporary.path()));
  REQUIRE(read_file(temporary.path()) == malformed);
  REQUIRE(matching_files(temporary.directory(), ".corrupt-").empty());
}
