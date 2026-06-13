#include <api/api.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <curl/curl.h>
#include <filesystem>
#include <helpers/tsqueue.hpp>
#include <helpers/utils.hpp>
#include <rfl/toml.hpp>
#include <sessions/handlers.hpp>
#include <state/config.hpp>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

using namespace wolf::api;
using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

std::string api_runtime_dir() {
  return utils::get_env("XDG_RUNTIME_DIR", "/tmp/");
}

std::string api_socket_path() {
  return (std::filesystem::path(api_runtime_dir()) / "wolf.sock").string();
}

/**
 * Perform a HTTP request using curl
 */
std::optional<std::pair<long /* response_code */, std::string /* raw message */>>
req(CURL *handle,
    HTTPMethod method,
    std::string_view target,
    std::string_view post_body = {},
    const std::vector<std::string> &header_params = {}) {
  logs::log(logs::trace, "[CURL] Sending [{}] -> {}", (int)method, target);
  curl_easy_setopt(handle, CURLOPT_URL, target.data());

  /* Set method */
  switch (method) {
  case HTTPMethod::GET:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "GET");
    break;
  case HTTPMethod::POST:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "POST");
    break;
  case HTTPMethod::PUT:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
    break;
  case HTTPMethod::DELETE:
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  }
  curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

  struct curl_slist *headers = nullptr;
  for (const auto &header : header_params) {
    headers = curl_slist_append(headers, header.c_str());
  }

  /* Pass POST params (if present) */
  if (method == HTTPMethod::POST && !post_body.empty()) {
    logs::log(logs::trace, "[CURL] POST: {}", post_body);

    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    headers = curl_slist_append(headers, "Content-type: application/json");
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, post_body.data());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, post_body.size());
  } else {
    curl_easy_setopt(handle, CURLOPT_POST, 0L);
  }
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

  /* Set custom writer (in order to receive back the response) */
  curl_easy_setopt(
      handle,
      CURLOPT_WRITEFUNCTION,
      static_cast<size_t (*)(char *, size_t, size_t, void *)>([](char *ptr, size_t size, size_t nmemb, void *read_buf) {
        *(static_cast<std::string *>(read_buf)) += std::string{ptr, size * nmemb};
        return size * nmemb;
      }));
  std::string read_buf;
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &read_buf);

  /* Run! */
  auto res = curl_easy_perform(handle);
  curl_slist_free_all(headers);
  if (res != CURLE_OK) {
    logs::log(logs::warning, "[CURL] Request failed with error: {}", curl_easy_strerror(res));
    return {};
  } else {
    long response_code;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    // Avoid printing binary output if the target is "/api/v1/utils/get-icon"
    if (target.find("/api/v1/utils/get-icon") == std::string::npos) {
      logs::log(logs::trace, "[CURL] Received {} - {}", response_code, read_buf);
    }
    return {{response_code, read_buf}};
  }
}

TEST_CASE("Pair APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = state::load_or_default("config.test.toml", event_bus, running_sessions);
  { // Avoid overriding the test config file (shared across multiple tests)
    config.config_source = "config.test.EDITED.toml";
    auto tml = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>("config.test.toml").value();
    rfl::toml::save(config.config_source, tml);
  }
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = immer::box<state::Config>(config),
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/pair/pending");
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true,\"requests\":[]}"));

  // Checkout the list of paired clients (there will be one in the test config file)
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/clients");
  REQUIRE(response);
  REQUIRE_THAT(response->second,
               Equals("{\"success\":true,\"clients\":["
                      "{\"client_id\":\"10594003729173467913\","
                      "\"app_state_folder\":\"some/folder\","
                      "\"settings\":{"
                      "\"run_uid\":1234,"
                      "\"run_gid\":5678,"
                      "\"controllers_override\":[\"PS\"],"
                      "\"mouse_acceleration\":2.5,"
                      "\"v_scroll_acceleration\":1.5,"
                      "\"h_scroll_acceleration\":10.199999809265137}}]}"));

  auto pair_promise = std::make_shared<boost::promise<std::string>>();

  // Simulate a Moonlight pairing request
  app_state->pairing_atom->update([pair_promise](auto pairing_map) {
    return pairing_map.set("secret",
                           immer::box<events::PairSignal>{
                               events::PairSignal{.client_ip = "1234", .host_ip = "5678", .user_pin = pair_promise}});
  });

  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/pair/pending");
  REQUIRE(response);
  REQUIRE_THAT(response->second,
               Equals("{\"success\":true,\"requests\":[{\"pair_secret\":\"secret\",\"client_ip\":\"1234\"}]}"));

  // Let's complete the pairing process
  response = req(curl.get(),
                 HTTPMethod::POST,
                 "http://localhost/api/v1/pair/client",
                 "{\"pair_secret\":\"secret\",\"pin\":\"1234\"}");
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
  REQUIRE(pair_promise->get_future().get() == "1234");
  REQUIRE(app_state->config.get().paired_clients->load().get().size() == 1);

  { // Test out if pairing with the same client_id overrides the previous client
    // https://github.com/games-on-whales/wolf/issues/211
    auto cfg = app_state->config.get();
    immer::box<state::PairedClient> previous_client = cfg.paired_clients->load().get()[0];
    state::pair(
        cfg,
        state::PairedClient{.client_cert = previous_client->client_cert, .app_state_folder = "ASDF", .settings = {}});
    REQUIRE(cfg.paired_clients->load().get().size() == 1);
    REQUIRE(cfg.paired_clients->load().get()[0]->app_state_folder == "ASDF");
  }

  REQUIRE(app_state->pairing_atom->load().get().size() == 0);

  { // Test out changing client settings
    response = req(curl.get(),
                   HTTPMethod::POST,
                   "http://localhost/api/v1/clients/settings",
                   "{\"client_id\":\"10594003729173467913\",\"app_state_folder\":\"OVERRIDDEN\", \"settings\":{}}");
    REQUIRE(response);
    REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
    REQUIRE_THAT(app_state->config.get().paired_clients->load().get()[0]->app_state_folder, Equals("OVERRIDDEN"));

    // Check back that we've correctly updated the config file
    auto tml = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(config.config_source).value();
    REQUIRE(tml.paired_clients[0].app_state_folder == "OVERRIDDEN");
  }

  { // Test out unpairing
    response = req(curl.get(),
                   HTTPMethod::POST,
                   "http://localhost/api/v1/unpair/client",
                   "{\"client_id\":\"10594003729173467913\"}");
    REQUIRE(response);
    REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

    // Check back that we've correctly updated the config file
    auto tml = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(config.config_source).value();
    REQUIRE(tml.paired_clients.empty());
  }
}

TEST_CASE("APPs APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = state::load_or_default("config.test.toml", event_bus, running_sessions);
  // Avoid overriding the test config file (shared across multiple tests)
  config.config_source = "config.test.EDITED.toml";
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = {config},
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  // Test that the initial list of apps matches what's in the test config file
  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/apps");
  REQUIRE(response);
  auto apps = rfl::json::read<AppListResponse>(response->second).value();
  REQUIRE(apps.success);
  REQUIRE(apps.apps.size() == 2);
  REQUIRE(apps.apps[0].title == "Firefox");
  REQUIRE(apps.apps[1].title == "Test ball");

  // Test that we can add an app
  auto app = rfl::Reflector<wolf::core::events::App>::ReflType{
      .title = "Test app",
      .id = "test",
      .support_hdr = false,
      .h264_gst_pipeline = "h264",
      .hevc_gst_pipeline = "hevc",
      .av1_gst_pipeline = "av1",
      .render_node = "render",
      .opus_gst_pipeline = "opus",
      .start_virtual_compositor = false,
      .runner = wolf::config::AppDocker{.name = "test",
                                        .image = "test",
                                        .mounts = {"/tmp:/tmp"},
                                        .env = {"LOG_LEVEL=1234"},
                                        .devices = {"/dev/input:/dev/input"},
                                        .ports = {"8080:8080"}}};
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/add", rfl::json::write(app));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

  // Test that the new app is in the list
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/apps");
  REQUIRE(response);
  auto apps2 = rfl::json::read<AppListResponse>(response->second).value();
  REQUIRE(apps2.success);
  REQUIRE(apps2.apps.size() == 3);
  REQUIRE(apps2.apps[2].title == "Test app");
  auto moonlight_profile = state::get_moonlight_profile(config);
  REQUIRE(moonlight_profile);
  immer::vector<immer::box<events::App>> wolf_apps = moonlight_profile.value()->apps->load();
  REQUIRE(wolf_apps.at(2)->base.title == "Test app");

  // Test that we can remove an app
  auto app_delete = AppDeleteRequest{.id = "test"};
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/delete", rfl::json::write(app_delete));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/apps");
  REQUIRE(response);
  auto apps3 = rfl::json::read<AppListResponse>(response->second).value();
  REQUIRE(apps3.success);
  REQUIRE(apps3.apps.size() == 2);
}

TEST_CASE("Profile APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = state::load_or_default("config.test.toml", event_bus, running_sessions);
  // Avoid overriding the test config file (shared across multiple tests)
  config.config_source = "config.test.EDITED.toml";
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = {config},
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  // Test the initial profile list only has the default user
  {
    auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/profiles");
    REQUIRE(response);
    REQUIRE_THAT(response->second,
                 Catch::Matchers::Equals(
                     R"({"success":true,"profiles":[{"name":"User","id":"user","icon_png_path":"","apps":[]}]})"));
  }

  // Test adding a profile
  {
    auto runner = std::make_shared<process::RunProcess>(event_bus, "destroy-all-humans.exe");
    auto apps = immer::vector<immer::box<events::App>>{
        events::App{.base = {.title = "Destroy All Humans", .id = "2", .support_hdr = false, .icon_png_path = ""},
                    .h264_gst_pipeline = "h264",
                    .hevc_gst_pipeline = "hevc",
                    .av1_gst_pipeline = "av1",
                    .render_node = "render_node",
                    .opus_gst_pipeline = "opus",
                    .start_virtual_compositor = true,
                    .start_audio_server = false,
                    .runner = runner}};
    auto profile = events::Profile{
        .id = "test",
        .name = "Test",
        .icon_png_path = "",
        .apps = std::make_shared<immer::atom<immer::vector<immer::box<events::App>>>>(apps),
    };
    auto response =
        req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/profiles/add", rfl::json::write(profile));
    REQUIRE(response);
    REQUIRE_THAT(response->second, Catch::Matchers::Equals(R"({"success":true})"));

    // Test the profile list now has the new profile
    auto response2 = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/profiles");
    REQUIRE(response2);
    auto profiles = rfl::json::read<ProfileListResponse>(response2->second);
    REQUIRE(profiles);
    REQUIRE(profiles.value().profiles.size() == 2);
    REQUIRE_THAT(profiles.value().profiles[1].name, Equals(profile.name));
    REQUIRE_THAT(profiles.value().profiles[1].id, Equals(profile.id));
    REQUIRE_THAT(profiles.value().profiles[1].icon_png_path, Equals(profile.icon_png_path));
    REQUIRE(profiles.value().profiles[1].apps.size() == 1);
  }

  // Test removing a profile
  {
    auto response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/profiles/remove", R"({"id":"test"})");
    REQUIRE(response);
    REQUIRE_THAT(response->second, Catch::Matchers::Equals(R"({"success":true})"));
    // Test the profile list now has the new profile
    auto response2 = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/profiles");
    REQUIRE(response2);
    auto profiles = rfl::json::read<ProfileListResponse>(response2->second);
    REQUIRE(profiles);
    REQUIRE(profiles.value().profiles.size() == 1);
    REQUIRE_THAT(profiles.value().profiles[0].name, Equals("User"));
  }
}

TEST_CASE("Sessions APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  // Test that the initial list of sessions is empty
  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  REQUIRE(response);
  auto sessions = rfl::json::read<StreamSessionListResponse>(response->second).value();
  REQUIRE(sessions.success);
  REQUIRE(sessions.sessions.size() == 0);

  // Test that we can add a session
  auto session = rfl::Reflector<wolf::core::events::StreamSession>::ReflType{
      .client_ip = "127.0.0.1",
      .app_id = "304556286",               // test cfg file
      .client_id = "10594003729173467913", // test cfg file
  };
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/add", rfl::json::write(session));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Catch::Matchers::ContainsSubstring("{\"success\":true,\"session_id\":"));

  // Test that the new session is in the list
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  REQUIRE(response);
  auto sessions2 = rfl::json::read<StreamSessionListResponse>(response->second).value();
  REQUIRE(sessions2.success);
  REQUIRE(sessions2.sessions.size() == 1);

  // TODO: breaks Github CI
  // // Test that we can send input to a session
  // auto input_request = StreamSessionHandleInputRequest{
  //     .session_id = "10594003729173467913",
  //     // example CONTROLLER_MULTI packet
  //     .input_packet_hex = "060222000000001E0C0000001A000000010014000010000000000000000000009C0000005500"};
  // response =
  //     req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/input", rfl::json::write(input_request));
  // REQUIRE(response);
  // REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

  // Test that we can pause a session
  auto pause_request = StreamSessionPauseRequest{.session_id = "10594003729173467913"};
  response =
      req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/pause", rfl::json::write(pause_request));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

  // Test that we can stop a session
  auto stop_request = StreamSessionStopRequest{.session_id = "10594003729173467913"};
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/stop", rfl::json::write(stop_request));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
}

TEST_CASE("Session APIs without app_id or client_id", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  // Test that the initial list of sessions is empty
  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  REQUIRE(response);
  auto sessions = rfl::json::read<StreamSessionListResponse>(response->second).value();
  REQUIRE(sessions.success);
  REQUIRE(sessions.sessions.size() == 0);

  // Test that we can add a session
  auto session = rfl::Reflector<wolf::core::events::StreamSession>::ReflType{
      .client_ip = "127.0.0.1",
  };

  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/add", rfl::json::write(session));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Catch::Matchers::ContainsSubstring("{\"success\":true,\"session_id\":"));

  // Test that the new session is in the list
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  REQUIRE(response);
  auto sessions2 = rfl::json::read<StreamSessionListResponse>(response->second).value();
  REQUIRE(sessions2.success);
  REQUIRE(sessions2.sessions.size() == 1);
}

TEST_CASE("Lobbies APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .lobbies = std::make_shared<immer::atom<immer::vector<events::Lobby>>>(),
      .running_sessions = running_sessions});
  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();

  // Setup the event bus handlers for the lobbies events
  auto lobbies_handlers = sessions::setup_lobbies_handlers(app_state, runtime_dir, {});

  // Wait for the server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(42));

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);
  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  { // Test that the initial list of lobbies is empty
    auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/lobbies");
    REQUIRE(response);
    auto sessions = rfl::json::read<LobbiesResponse>(response->second).value();
    REQUIRE(sessions.success);
    REQUIRE(sessions.lobbies.size() == 0);
  }

  std::string lobby_id;
  std::vector<short> pin = {1, 2, 3, 4};
  { // Test creating a lobby
    auto new_lobby = CreateLobbyRequest{.profile_id = "test_profile",
                                        .name = "test_lobby",
                                        .multi_user = false,
                                        .pin = pin,
                                        .stop_when_everyone_leaves = false,
                                        .video_settings = {.width = 1920,
                                                           .height = 1080,
                                                           .refresh_rate = 60,
                                                           .wayland_render_node = "software",
                                                           .runner_render_node = "runner_render_node",
                                                           .video_producer_buffer_caps = "video/x-raw"},
                                        .audio_settings = {.channel_count = 2},
                                        .runner_state_folder = "runner_state_folder",
                                        .runner = wolf::config::AppCMD{.run_cmd = "sleep 10"}};
    auto response =
        req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/lobbies/create", rfl::json::write(new_lobby));
    REQUIRE(response);
    auto lobby_res = rfl::json::read<LobbyCreateResponse>(response->second).value();
    REQUIRE(lobby_res.success);
    REQUIRE(!lobby_res.lobby_id.empty());
    lobby_id = lobby_res.lobby_id;
    // Test that the lobby is listed in the list of lobbies
    response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/lobbies");
    REQUIRE(response);
    auto sessions = rfl::json::read<LobbiesResponse>(response->second).value();
    REQUIRE(sessions.success);
    REQUIRE(sessions.lobbies.size() == 1);
    REQUIRE_THAT(sessions.lobbies[0].id, Equals(lobby_res.lobby_id));
    REQUIRE(sessions.lobbies[0].connected_sessions.size() == 0);
  }

  std::size_t moonlight_session_id = 1234;
  { // Test joining a lobby
    // First, create a StreamSession
    running_sessions->update([moonlight_session_id](const immer::vector<events::StreamSession> &sessions) {
      return sessions.push_back(events::StreamSession{.session_id = moonlight_session_id});
    });
    // Then, call the join endpoint
    auto payload =
        rfl::json::write(events::JoinLobbyEvent{.lobby_id = lobby_id, .moonlight_session_id = moonlight_session_id});
    auto response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/lobbies/join", payload);
    REQUIRE(response);
    auto error_res = rfl::json::read<GenericErrorResponse>(response->second).value();
    // we expect this to fail since we don't pass the PIN!
    REQUIRE(!error_res.success);

    // Now call it with the right PIN
    response = req(
        curl.get(),
        HTTPMethod::POST,
        "http://localhost/api/v1/lobbies/join",
        rfl::json::write(
            events::JoinLobbyEvent{.lobby_id = lobby_id, .moonlight_session_id = moonlight_session_id, .pin = pin}));
    REQUIRE(response);
    auto join_res = rfl::json::read<GenericSuccessResponse>(response->second).value();
    REQUIRE(join_res.success);

    // Test that the lobby now lists this session
    response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/lobbies");
    REQUIRE(response);
    auto sessions = rfl::json::read<LobbiesResponse>(response->second).value();
    REQUIRE(sessions.success);
    REQUIRE(sessions.lobbies.size() == 1);
    REQUIRE_THAT(sessions.lobbies[0].id, Equals(lobby_id));
    REQUIRE(sessions.lobbies[0].connected_sessions.size() == 1);
    REQUIRE_THAT(sessions.lobbies[0].connected_sessions[0], Equals(std::to_string(moonlight_session_id)));
  }

  { // Test leaving a lobby
    auto response = req(
        curl.get(),
        HTTPMethod::POST,
        "http://localhost/api/v1/lobbies/leave",
        rfl::json::write(events::LeaveLobbyEvent{.lobby_id = lobby_id, .moonlight_session_id = moonlight_session_id}));
    REQUIRE(response);
    auto leave_res = rfl::json::read<GenericSuccessResponse>(response->second).value();
    REQUIRE(leave_res.success);
    // Test that the lobby no longer lists this session
    response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/lobbies");
    REQUIRE(response);
    auto sessions = rfl::json::read<LobbiesResponse>(response->second).value();
    REQUIRE(sessions.success);
    REQUIRE(sessions.lobbies.size() == 1);
    REQUIRE_THAT(sessions.lobbies[0].id, Equals(lobby_id));
    REQUIRE(sessions.lobbies[0].connected_sessions.empty());
  }

  { // Test stopping the lobby
    auto response = req(curl.get(),
                        HTTPMethod::POST,
                        "http://localhost/api/v1/lobbies/stop",
                        rfl::json::write(events::StopLobbyEvent{.lobby_id = lobby_id}));
    REQUIRE(response);
    auto error_res = rfl::json::read<GenericSuccessResponse>(response->second).value();
    // We expect this to fail since we don't pass a PIN!
    REQUIRE(!error_res.success);
    // Now try again with the PIN
    response = req(curl.get(),
                   HTTPMethod::POST,
                   "http://localhost/api/v1/lobbies/stop",
                   rfl::json::write(events::StopLobbyEvent{.lobby_id = lobby_id, .pin = pin}));
    REQUIRE(response);
    auto stop_res = rfl::json::read<GenericSuccessResponse>(response->second).value();
    REQUIRE(stop_res.success);

    // Test that the lobby no longer exists
    response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/lobbies");
    REQUIRE(response);
    auto sessions = rfl::json::read<LobbiesResponse>(response->second).value();
    REQUIRE(sessions.success);
    REQUIRE(sessions.lobbies.empty());
  }

  // Give it a few seconds to turn down the Gstreamer pipeline
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

TEST_CASE("Utils APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .lobbies = std::make_shared<immer::atom<immer::vector<events::Lobby>>>(),
      .running_sessions = running_sessions});
  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();

  // Wait for the server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(42));

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);
  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  { // Test that calling
    // http://localhost/api/v1/utils/get-icon\?icon_path\=https://games-on-whales.github.io/wildlife/apps/firefox/assets/icon.png
    // returns the content of the PNG file
    auto response = req(curl.get(),
                        HTTPMethod::GET,
                        "http://localhost/api/v1/utils/get-icon?icon_path=https://games-on-whales.github.io/wildlife/"
                        "apps/firefox/assets/icon.png");

    REQUIRE(response);
    REQUIRE_THAT(response->second, Catch::Matchers::ContainsSubstring("PNG"));
  }

  { // Test that when getting a non 200 from a remote resource we don't return an icon
    auto response = req(curl.get(),
                        HTTPMethod::GET,
                        "http://localhost/api/v1/utils/get-icon?icon_path=https://games-on-whales.github.io/wildlife/"
                        "THIS_PAGE_DOES_NOT_EXISTS");

    REQUIRE(response);
    REQUIRE(response->first == 404);
  }
}

struct SSEEvent {
  std::string event;
  std::string data;
};

void listen_sse(CURL *handle, std::shared_ptr<TSQueue<SSEEvent>> queue, std::string_view api_endpoint) {
  curl_easy_setopt(handle, CURLOPT_URL, api_endpoint.data());
  curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  curl_easy_setopt(
      handle,
      CURLOPT_WRITEFUNCTION,
      static_cast<size_t (*)(char *, size_t, size_t, void *)>([](char *ptr, size_t size, size_t nmemb, void *tsqueue) {
        std::string data{ptr, size * nmemb};
        logs::log(logs::debug, "[CURL] Received: {}", data);

        auto ts_queue = static_cast<TSQueue<SSEEvent> *>(tsqueue);
        auto lines = utils::split(data, '\n');
        if (lines.size() >= 2 && lines[0].starts_with("event: ") && lines[1].starts_with("data: ")) {
          ts_queue->push(SSEEvent{.event = std::string(lines[0].substr(7)), .data = std::string(lines[1].substr(6))});
        } else {
          logs::log(logs::warning, "[CURL] Invalid SSE event: {}", data);
        }

        return size * nmemb;
      }));
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, reinterpret_cast<void *>(queue.get()));

  curl_easy_perform(handle);
}

TEST_CASE("SSE APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto runtime_dir = api_runtime_dir();
  auto socket_path = api_socket_path();
  // Start the server
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());

  auto queue = std::make_shared<TSQueue<SSEEvent>>();

  std::thread sse_thread(listen_sse, curl.get(), queue, "http://localhost/api/v1/events");
  sse_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the SSE client to start

  // Test out one of the events
  event_bus->fire_event(events::IDRRequestEvent{.session_id = 42});

  auto event = queue->pop();
  REQUIRE(event.has_value());
  REQUIRE_THAT(event->event, Equals("wolf::core::events::IDRRequestEvent"));
  REQUIRE_THAT(event->data, Equals("{\"session_id\":\"42\"}"));
}
