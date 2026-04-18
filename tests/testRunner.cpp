#include <catch2/catch_test_macros.hpp>

#include <events/events.hpp>
#include <sessions/handlers.hpp>
#include <state/data-structures.hpp>
#include <map>
#include <memory>
#include <optional>

using namespace wolf::core;

namespace {

class RecordingRunner : public events::Runner {
public:
  std::map<std::string, std::string> env;

  void run(std::string_view,
           std::string_view,
           std::string_view,
           std::shared_ptr<events::devices_atom_queue>,
           const immer::array<std::string> &,
           const immer::array<std::pair<std::string, std::string>> &,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view) override {
    for (const auto &[key, value] : env_variables) {
      env.emplace(key, value);
    }
  }

  events::RunnerTypes serialize() const override {
    return config::AppCMD{"true"};
  }
};

} // namespace

TEST_CASE("Session runners use the retro user", "[Runner]") {
  auto runner = std::make_shared<RecordingRunner>();
  auto plugged_devices_queue = std::make_shared<events::devices_atom_queue>();

  auto host = immer::box<state::Host>{state::Host{{},
                                                  {},
                                                  nullptr,
                                                  nullptr,
                                                  {},
                                                  {},
                                                  "/tmp/wolf-host-state",
                                                  "/tmp/wolf-local-state",
                                                  "/tmp/wolf-runtime"}};

  auto client_settings = immer::box<config::ClientSettings>{config::ClientSettings{1000, 1000, {}, 1.0f, 1.0f, 1.0f}};

  const std::string session_id = "session-123";
  const std::string app_local_state_folder = "/tmp/wolf-app-local";
  const std::string app_host_state_folder = "/tmp/wolf-app-host";
  const std::string xdg_runtime_dir = "/run/user/wolf";
  const std::optional<sessions::AudioServer> audio_server = std::nullopt;
  const std::shared_ptr<audio::VSink> audio_sink = nullptr;
  const events::VideoSettings video_settings{1920, 1080, 60, "", "/tmp/does-not-exist", "video/x-raw"};

  sessions::start_runner(runner,
                         plugged_devices_queue,
                         immer::box<sessions::RunnerArgs>{sessions::RunnerArgs{session_id,
                                                                              video_settings,
                                                                              nullptr,
                                                                              audio_server,
                                                                              audio_sink,
                                                                              host,
                                                                              app_local_state_folder,
                                                                              app_host_state_folder,
                                                                              xdg_runtime_dir,
                                                                              client_settings}});

  REQUIRE(runner->env.at("PUID") == "1000");
  REQUIRE(runner->env.at("PGID") == "1000");
  REQUIRE(runner->env.at("UNAME") == "retro");
}
