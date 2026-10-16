#include <api/api.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <poll.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <helpers/tsqueue.hpp>
#include <helpers/utils.hpp>
#include <rfl/toml.hpp>
#include <sessions/handlers.hpp>
#include <state/config.hpp>
#include <state/sessions.hpp>

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
 * Start the API server and wait until it accepts connections.
 * Servers started by earlier tests keep running: their socket file is removed first, so that we can't reach one of
 * them instead of the new server.
 */
void start_api_server(const immer::box<state::AppState> &app_state) {
  auto socket_path = api_socket_path();
  std::filesystem::remove(socket_path);
  std::thread([app_state, runtime_dir = api_runtime_dir()]() {
    wolf::api::start_server(runtime_dir, app_state);
  }).detach();
  for (int attempt = 0; attempt < 500; attempt++) {
    boost::asio::io_context ctx;
    boost::asio::local::stream_protocol::socket probe(ctx);
    boost::system::error_code ec;
    probe.connect(boost::asio::local::stream_protocol::endpoint(socket_path), ec);
    if (!ec) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  FAIL("The API server didn't start");
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
    logs::log(logs::trace, "[CURL] POST: {}", post_body.substr(0, 1024));

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
      logs::log(logs::trace, "[CURL] Received {} - {}", response_code, std::string_view(read_buf).substr(0, 1024));
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
  start_api_server(app_state);

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
                      "\"h_scroll_acceleration\":10.199999809265137,"
                      "\"motion_controller_override\":\"AUTO\"}}]}"));

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
  start_api_server(app_state);

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
  REQUIRE(apps.apps[0].video_producer_buffer_caps.has_value());
  REQUIRE_FALSE(apps.apps[0].video_producer_buffer_caps->empty());

  // Test that we can add an app
  auto app = rfl::Reflector<wolf::core::events::App>::ReflType{
      .title = "Test app",
      .id = "test",
      .support_hdr = false,
      .video_producer_buffer_caps = "video/x-raw(memory:DMABuf)",
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
  REQUIRE(apps2.apps[2].video_producer_buffer_caps == "video/x-raw(memory:DMABuf)");
  auto moonlight_profile = state::get_moonlight_profile(config);
  REQUIRE(moonlight_profile);
  immer::vector<immer::box<events::App>> wolf_apps = moonlight_profile.value()->apps->load();
  REQUIRE(wolf_apps.at(2)->base.title == "Test app");
  REQUIRE(wolf_apps.at(2)->video_producer_buffer_caps == "video/x-raw(memory:DMABuf)");

  // Test that apps survive a restart: reloading the saved config gives back the same apps
  {
    auto reloaded = state::load_or_default(config.config_source, event_bus, running_sessions);
    immer::vector<immer::box<events::App>> reloaded_apps = state::get_moonlight_profile(reloaded).value()->apps->load();
    REQUIRE(reloaded_apps.size() == 3);
    for (auto i = 0; i < 3; i++) {
      REQUIRE(reloaded_apps.at(i)->base.title == wolf_apps.at(i)->base.title);
      REQUIRE(reloaded_apps.at(i)->base.id == wolf_apps.at(i)->base.id);
      REQUIRE(reloaded_apps.at(i)->video_producer_buffer_caps == wolf_apps.at(i)->video_producer_buffer_caps);
      REQUIRE(reloaded_apps.at(i)->h264_gst_pipeline == wolf_apps.at(i)->h264_gst_pipeline);
      REQUIRE(reloaded_apps.at(i)->opus_gst_pipeline == wolf_apps.at(i)->opus_gst_pipeline);
      REQUIRE(reloaded_apps.at(i)->render_node == wolf_apps.at(i)->render_node);
      REQUIRE(reloaded_apps.at(i)->start_virtual_compositor == wolf_apps.at(i)->start_virtual_compositor);
    }
    REQUIRE(reloaded_apps.at(2)->base.id == "test");
    REQUIRE(reloaded_apps.at(2)->video_producer_buffer_caps == "video/x-raw(memory:DMABuf)");
    REQUIRE(reloaded_apps.at(2)->h264_gst_pipeline == "h264");
    REQUIRE(reloaded_apps.at(2)->opus_gst_pipeline == "opus");
    if (!reloaded_apps.at(0)->hevc_gst_pipeline.empty()) { // HEVC encoder available
      REQUIRE(reloaded_apps.at(2)->hevc_gst_pipeline == "hevc");
    }
    if (!reloaded_apps.at(0)->av1_gst_pipeline.empty()) { // AV1 encoder available
      REQUIRE(reloaded_apps.at(2)->av1_gst_pipeline == "av1");
    }

    // Apps from the config file keep their entry as written, and defaults aren't frozen into the file
    auto tml = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(config.config_source).value();
    auto tml_profile = std::find_if(tml.profiles.begin(), tml.profiles.end(), [](const auto &p) {
      return p.id == events::MOONLIGHT_PROFILE_ID;
    });
    REQUIRE(tml_profile != tml.profiles.end());
    REQUIRE_FALSE(tml_profile->apps.at(0).video.has_value());
    REQUIRE_FALSE(tml_profile->apps.at(0).id.has_value());
    auto pristine = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>("config.test.toml").value();
    auto pristine_profile = std::find_if(pristine.profiles.begin(), pristine.profiles.end(), [](const auto &p) {
      return p.id == events::MOONLIGHT_PROFILE_ID;
    });
    REQUIRE(rfl::json::write(tml_profile->apps.at(0)) == rfl::json::write(pristine_profile->apps.at(0)));
    REQUIRE(rfl::json::write(tml_profile->apps.at(1)) == rfl::json::write(pristine_profile->apps.at(1)));
    REQUIRE(tml_profile->apps.at(1).video->source == "override DEFAULT SOURCE");
    REQUIRE_FALSE(tml_profile->apps.at(1).video->h264_pipeline.has_value());
    REQUIRE(tml_profile->apps.at(2).id == "test");
    REQUIRE_FALSE(tml_profile->apps.at(2).video->source.has_value());
  }

  // An app added with this host's default pipelines doesn't get them frozen into the config file
  {
    auto firefox = rfl::Reflector<events::App>::from(wolf_apps.at(0));
    auto defaults_app = rfl::Reflector<events::App>::ReflType{
        .title = "Defaults app",
        .id = "defaults",
        .support_hdr = false,
        .video_producer_buffer_caps = firefox.video_producer_buffer_caps,
        .h264_gst_pipeline = firefox.h264_gst_pipeline,
        .hevc_gst_pipeline = firefox.hevc_gst_pipeline,
        .av1_gst_pipeline = firefox.av1_gst_pipeline,
        .render_node = firefox.render_node,
        .opus_gst_pipeline = firefox.opus_gst_pipeline,
        .start_virtual_compositor = true,
        .start_audio_server = true,
        .runner = firefox.runner};
    response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/add", rfl::json::write(defaults_app));
    REQUIRE(response);
    REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

    auto tml = rfl::toml::load<wolf::config::WolfConfig, rfl::DefaultIfMissing>(config.config_source).value();
    auto tml_profile = std::find_if(tml.profiles.begin(), tml.profiles.end(), [](const auto &p) {
      return p.id == events::MOONLIGHT_PROFILE_ID;
    });
    REQUIRE(tml_profile->apps.at(3).title == "Defaults app");
    REQUIRE(tml_profile->apps.at(3).id == "defaults");
    REQUIRE_FALSE(tml_profile->apps.at(3).video.has_value());
    REQUIRE_FALSE(tml_profile->apps.at(3).audio.has_value());
    REQUIRE_FALSE(tml_profile->apps.at(3).render_node.has_value());

    response = req(curl.get(),
                   HTTPMethod::POST,
                   "http://localhost/api/v1/apps/delete",
                   rfl::json::write(AppDeleteRequest{.id = "defaults"}));
    REQUIRE(response);
  }

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

  // Apps added without (or with blank) producer caps fall back to video/x-raw
  for (const auto &caps : {std::optional<std::string>{},
                           std::optional<std::string>{""},
                           std::optional<std::string>{" "},
                           std::optional<std::string>{"\t\n"},
                           std::optional<std::string>{"\v\f"}}) {
    auto default_app = app;
    default_app.video_producer_buffer_caps = caps;
    response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/add", rfl::json::write(default_app));
    REQUIRE(response);
    REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
    wolf_apps = moonlight_profile.value()->apps->load();
    REQUIRE(wolf_apps.at(2)->video_producer_buffer_caps == "video/x-raw");
    response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/delete", rfl::json::write(app_delete));
    REQUIRE(response);
  }
  // Replies bigger than a single socket write must arrive intact
  auto big_app = app;
  big_app.h264_gst_pipeline = std::string(4 * 1024 * 1024, 'x');
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/add", rfl::json::write(big_app));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
#ifdef __GLIBC__
  mallopt(M_PERTURB, 165); // scrub freed memory, so a reply sent from a freed buffer is reliably corrupted
#endif
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/apps");
#ifdef __GLIBC__
  mallopt(M_PERTURB, 0);
#endif
  REQUIRE(response);
  auto apps4 = rfl::json::read<AppListResponse>(response->second);
  REQUIRE(apps4);
  REQUIRE(apps4.value().apps.size() == 3);
  REQUIRE(apps4.value().apps[2].h264_gst_pipeline == big_app.h264_gst_pipeline);
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/apps/delete", rfl::json::write(app_delete));
  REQUIRE(response);
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
  start_api_server(app_state);

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
  start_api_server(app_state);

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
  // The session fields stay at the top level, next to its state
  REQUIRE(sessions2.sessions[0].session.get().client_ip == "127.0.0.1");
  REQUIRE_THAT(response->second, ContainsSubstring("\"client_ip\":\"127.0.0.1\""));
  REQUIRE(sessions2.sessions[0].state.name() == "setup");

  // The listed state follows the stream's: streaming once both pipelines run, paused after a pause
  auto stream_state = running_sessions->load()->front().stream_state;
  auto listed_state = [&]() {
    auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
    REQUIRE(response);
    return rfl::json::read<StreamSessionListResponse>(response->second).value().sessions[0].state.name();
  };
  stream_state->update([](auto state) {
    state.video_pipelines++;
    return state;
  });
  REQUIRE(listed_state() == "setup");
  stream_state->update([](auto state) {
    state.audio_pipelines++;
    return state;
  });
  REQUIRE(listed_state() == "streaming");
  stream_state->update([](auto state) {
    state.paused = true;
    return state;
  });
  REQUIRE(listed_state() == "paused");

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

/**
 * A stand-in for a stream's GStreamer pipeline: it runs until its session is paused or stopped, or until its stream
 * instance is superseded, then takes `teardown` to finish, as a real pipeline does while it drains to EOS.
 */
struct FakePipelines {
  std::shared_ptr<events::EventBusType> ev_bus;
  std::chrono::milliseconds teardown;
  std::shared_ptr<std::atomic_int> started = std::make_shared<std::atomic_int>(0);
  std::shared_ptr<std::atomic_int> ended = std::make_shared<std::atomic_int>(0);

  void run(std::size_t session_id, std::array<char, 16> rtp_secret) const {
    auto end_signal = std::make_shared<std::promise<void>>();
    auto end_future = end_signal->get_future();
    auto signalled = std::make_shared<std::atomic_bool>(false);
    auto end = [end_signal, signalled]() {
      if (!signalled->exchange(true)) {
        end_signal->set_value();
      }
    };
    auto pause = ev_bus->register_handler<immer::box<events::PauseStreamEvent>>(
        [=](const immer::box<events::PauseStreamEvent> &ev) {
          if (ev->session_id == session_id)
            end();
        });
    auto stop = ev_bus->register_handler<immer::box<events::StopStreamEvent>>(
        [=](const immer::box<events::StopStreamEvent> &ev) {
          if (ev->session_id == session_id)
            end();
        });
    auto superseded = ev_bus->register_handler<immer::box<events::EndSupersededStreamEvent>>(
        [=](const immer::box<events::EndSupersededStreamEvent> &ev) {
          if (ev->session_id == session_id && ev->rtp_secret_payload == rtp_secret)
            end();
        });
    (*started)++;
    end_future.wait();
    pause.unregister();
    stop.unregister();
    superseded.unregister();
    std::this_thread::sleep_for(teardown);
    (*ended)++;
  }

  sessions::StreamPipelines pipelines() const {
    return {.video = [*this](const immer::box<events::VideoSession> &sess,
                             const auto &) { run(sess->session_id, sess->rtp_secret_payload); },
            .audio = [*this](const immer::box<events::AudioSession> &sess,
                             const auto &) { run(sess->session_id, sess->rtp_secret_payload); }};
  }
};

TEST_CASE("Sessions API: the listed state follows the stream's events", "[API]") {
  using namespace std::chrono_literals;
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  auto teardown = 500ms;
  auto fake = FakePipelines{.ev_bus = event_bus, .teardown = teardown};
  auto pipeline_handlers = sessions::setup_stream_pipeline_handlers(event_bus, running_sessions, fake.pipelines());

  // Each run (one per SECTION) gets its own socket: the previous run's server is still listening on its own.
  // Kept short under /tmp, whatever XDG_RUNTIME_DIR is, so the socket path fits in sockaddr_un.
  char runtime_dir_template[] = "/tmp/wolf-test-XXXXXX";
  REQUIRE(mkdtemp(runtime_dir_template));
  auto runtime_dir = std::string(runtime_dir_template);
  auto remove_runtime_dir = std::shared_ptr<void>(nullptr, [runtime_dir](void *) {
    std::error_code ec;
    std::filesystem::remove_all(runtime_dir, ec);
  });
  auto socket_path = (std::filesystem::path(runtime_dir) / "wolf.sock").string();
  std::thread server_thread([app_state, runtime_dir]() { wolf::api::start_server(runtime_dir, app_state); });
  server_thread.detach();

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);
  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  // Wait for the server to start
  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  for (int i = 0; i < 250 && !response; i++) {
    std::this_thread::sleep_for(20ms);
    response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  }
  REQUIRE(response);

  auto session = rfl::Reflector<wolf::core::events::StreamSession>::ReflType{
      .client_ip = "127.0.0.1",
      .app_id = "304556286",               // test cfg file
      .client_id = "10594003729173467913", // test cfg file
  };
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/add", rfl::json::write(session));
  REQUIRE(response);
  REQUIRE_THAT(response->second, ContainsSubstring("{\"success\":true,\"session_id\":"));
  REQUIRE(running_sessions->load()->size() == 1);
  auto session_id = running_sessions->load()->front().session_id;

  auto listed_state = [&]() {
    auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
    REQUIRE(response);
    auto sessions = rfl::json::read<StreamSessionListResponse>(response->second).value();
    REQUIRE(sessions.sessions.size() == 1);
    return std::string(sessions.sessions[0].state.name());
  };
  auto rtp_secret = [](char id) { return std::array<char, 16>{id}; };
  auto video_session = [&](char secret) {
    events::VideoSession video{.session_id = session_id};
    video.rtp_secret_payload = rtp_secret(secret);
    return video;
  };
  auto audio_session = [&](char secret) {
    events::AudioSession audio{.session_id = session_id};
    audio.rtp_secret_payload = rtp_secret(secret);
    return audio;
  };
  // A real client keeps pinging until its stream starts: the handlers start listening for pings on another thread
  auto ping_until_started = [&](char secret, int started) {
    for (int i = 0; i < 250 && *fake.started < started; i++) {
      event_bus->fire_event(immer::box<events::RTPVideoPingEvent>(
          events::RTPVideoPingEvent{.client_ip = "127.0.0.1", .client_port = 9, .payload = rtp_secret(secret)}));
      event_bus->fire_event(immer::box<events::RTPAudioPingEvent>(
          events::RTPAudioPingEvent{.client_ip = "127.0.0.1", .client_port = 9, .payload = rtp_secret(secret)}));
      std::this_thread::sleep_for(20ms);
    }
    REQUIRE(*fake.started == started);
  };
  // RTSP setup of a stream instance
  auto setup = [&](char secret) {
    event_bus->fire_event(immer::box<events::VideoSession>(video_session(secret)));
    event_bus->fire_event(immer::box<events::AudioSession>(audio_session(secret)));
  };

  REQUIRE(listed_state() == "setup");
  setup(1);
  REQUIRE(listed_state() == "setup"); // until the client's first ping
  // A pause before the first ping, with no pipeline to end yet, doesn't hold the stream paused once it starts
  event_bus->fire_event(immer::box<events::PauseStreamEvent>(events::PauseStreamEvent{.session_id = session_id}));
  REQUIRE(listed_state() == "paused");
  ping_until_started(1, 2);
  REQUIRE(listed_state() == "streaming");

  SECTION("an API start during a slow teardown after a pause stays streaming") {
    event_bus->fire_event(immer::box<events::PauseStreamEvent>(events::PauseStreamEvent{.session_id = session_id}));
    // The old pipelines are still tearing down, but the stream is paused
    REQUIRE(*fake.ended == 0);
    REQUIRE(listed_state() == "paused");

    auto start_request = StreamSessionStartRequest{.session_id = std::to_string(session_id),
                                                   .video_session = video_session(2),
                                                   .audio_session = audio_session(2)};
    response =
        req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/start", rfl::json::write(start_request));
    REQUIRE(response);
    REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
    REQUIRE(listed_state() != "paused"); // set up again, before its first ping
    ping_until_started(2, 4);
    REQUIRE(*fake.ended == 0);
    REQUIRE(listed_state() == "streaming");

    // The paused pipelines finishing their teardown doesn't end the restarted stream
    std::this_thread::sleep_for(teardown + 300ms);
    REQUIRE(*fake.ended == 2);
    REQUIRE(listed_state() == "streaming");
  }

  SECTION("a resume replaces the session, and ending the superseded stream drains only the old instance") {
    // What /resume does: a new StreamSession, with its own stream state, under the same id
    running_sessions->update([](const immer::vector<events::StreamSession> &sessions) {
      auto resumed = sessions.front();
      resumed.stream_state = std::make_shared<immer::atom<events::StreamState>>();
      return state::remove_session(sessions, sessions.front()).push_back(resumed);
    });
    REQUIRE(listed_state() == "setup"); // even though the old instance is still streaming

    setup(2);
    ping_until_started(2, 4);
    REQUIRE(listed_state() == "streaming");

    event_bus->fire_event(immer::box<events::EndSupersededStreamEvent>(
        events::EndSupersededStreamEvent{.session_id = session_id, .rtp_secret_payload = rtp_secret(1)}));
    std::this_thread::sleep_for(teardown + 300ms);
    REQUIRE(*fake.ended == 2); // only the old instance's
    REQUIRE(listed_state() == "streaming");
  }

  // End the remaining pipelines
  event_bus->fire_event(immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = session_id}));
  std::this_thread::sleep_for(teardown + 300ms);
  REQUIRE(*fake.ended == *fake.started);
  REQUIRE(listed_state() == "setup");
  // Every pipeline counted itself out, which the listed state alone can't tell once either count is 0
  auto stream_state = running_sessions->load()->front().stream_state->load();
  REQUIRE(stream_state->video_pipelines == 0);
  REQUIRE(stream_state->audio_pipelines == 0);
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
  start_api_server(app_state);

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

// A device unplugged while its session is inside a lobby must be relayed to the lobby's runner.
TEST_CASE("Lobby relays a device unplug to its runner", "[API]") {
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

  // A lobby with one connected Moonlight session, added straight to state (no compositor).
  const std::string lobby_id = "lobby-under-test";
  const std::string moonlight_session_id = "1234";
  events::Lobby lobby{.id = lobby_id,
                      .name = "test_lobby",
                      .started_by_profile_id = "test_profile",
                      .multi_user = false,
                      .stop_when_everyone_leaves = false};
  lobby.connected_sessions->update([&](const immer::vector<immer::box<std::string>> &connected) {
    return connected.push_back(immer::box<std::string>{moonlight_session_id});
  });
  app_state->lobbies->update([&](const immer::vector<events::Lobby> &lobbies) { return lobbies.push_back(lobby); });

  auto lobbies_handlers = sessions::setup_lobbies_handlers(app_state, api_runtime_dir(), {});

  int relayed_to_lobby = 0;
  auto observer = event_bus->register_handler<immer::box<events::UnplugDeviceEvent>>(
      [&](const immer::box<events::UnplugDeviceEvent> &ev) {
        if (ev->session_id == lobby_id) {
          relayed_to_lobby++;
        }
      });

  event_bus->fire_event(
      immer::box<events::UnplugDeviceEvent>{events::UnplugDeviceEvent{.session_id = moonlight_session_id}});

  REQUIRE(relayed_to_lobby == 1);
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
  start_api_server(app_state);

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());

  auto queue = std::make_shared<TSQueue<SSEEvent>>();

  std::thread sse_thread(listen_sse, curl.get(), queue, "http://localhost/api/v1/events");
  sse_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the SSE client to start

  // Test out one of the events
  // The subscriber is registered asynchronously: an event fired before that reaches nobody, so retry until one lands
  std::optional<SSEEvent> event;
  for (int attempt = 0; attempt < 50 && !event; attempt++) {
    event_bus->fire_event(events::IDRRequestEvent{.session_id = 42});
    event = queue->pop();
  }
  REQUIRE(event.has_value());
  REQUIRE_THAT(event->event, Equals("wolf::core::events::IDRRequestEvent"));
  REQUIRE_THAT(event->data, Equals("{\"session_id\":\"42\"}"));
}

TEST_CASE("SSE APIs large events", "[API]") {
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
  start_api_server(app_state);

  boost::asio::io_context client_ctx;
  boost::asio::local::stream_protocol::socket client(client_ctx);
  client.connect(boost::asio::local::stream_protocol::endpoint(socket_path));
  boost::asio::write(client, boost::asio::buffer(std::string_view("GET /api/v1/events HTTP/1.0\r\n\r\n")));
  boost::asio::streambuf headers;
  boost::asio::read_until(client, headers, "\r\n\r\n");
  std::string received(boost::asio::buffers_begin(headers.data()), boost::asio::buffers_end(headers.data()));
  received = received.substr(received.find("\r\n\r\n") + 4);

  // Each event is bigger than the socket buffer, so the first write is still in flight when the second one is queued
  auto first = events::DockerContainerCreated{.container_id = std::string(1024 * 1024, 'a')};
  auto second = events::DockerContainerCreated{.container_id = std::string(1024 * 1024, 'b')};
  auto as_sse = [](const events::DockerContainerCreated &ev) {
    return fmt::format("event: wolf::core::events::DockerContainerCreated\ndata: {}\n\n", rfl::json::write(ev));
  };
  auto expected = as_sse(first) + as_sse(second);
#ifdef __GLIBC__
  mallopt(M_PERTURB, 165); // scrub freed memory, so an event sent from a freed buffer is reliably corrupted
#endif
  event_bus->fire_event(immer::box<events::DockerContainerCreated>(first));
  event_bus->fire_event(immer::box<events::DockerContainerCreated>(second));

  std::array<char, 64 * 1024> chunk{};
  while (received.size() < expected.size()) {
    received.append(chunk.data(), client.read_some(boost::asio::buffer(chunk)));
    // A keepalive may legitimately land between two events
    for (auto pos = received.find(":keepalive\n\n"); pos != std::string::npos; pos = received.find(":keepalive\n\n")) {
      received.erase(pos, 12);
    }
  }
#ifdef __GLIBC__
  mallopt(M_PERTURB, 0);
#endif
  REQUIRE(received.size() == expected.size());
  // Compare by offset: printing two 2 MiB strings on failure is useless
  auto first_mismatch = std::mismatch(received.begin(), received.end(), expected.begin()).first - received.begin();
  REQUIRE(first_mismatch == static_cast<long>(expected.size()));

  {
    // A subscriber that keeps up gets every event, even when a single one is bigger than MAX_WRITE_QUEUE_BYTES
    auto huge_event = events::DockerContainerCreated{.container_id = std::string(65 * 1024 * 1024, 'h')};
    auto huge_expected = as_sse(huge_event) + as_sse(huge_event);
    std::string huge_received;
    huge_received.reserve(huge_expected.size());
    // Both are queued before we start reading: the first one is still in flight when the second one arrives
    event_bus->fire_event(immer::box<events::DockerContainerCreated>(huge_event));
    event_bus->fire_event(immer::box<events::DockerContainerCreated>(huge_event));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::vector<char> buf(1024 * 1024);
    boost::system::error_code read_ec;
    while (huge_received.size() < huge_expected.size() && !read_ec) {
      huge_received.append(buf.data(), client.read_some(boost::asio::buffer(buf), read_ec));
    }
    for (auto pos = huge_received.find(":keepalive\n\n"); pos != std::string::npos;
         pos = huge_received.find(":keepalive\n\n")) {
      huge_received.erase(pos, 12);
    }
    REQUIRE(huge_received.size() == huge_expected.size());
    REQUIRE(huge_received.compare(huge_expected) == 0);
  }

  // A subscriber that stops reading is dropped instead of having every event buffered for it
  auto big_event = events::DockerContainerCreated{.container_id = std::string(1024 * 1024, 'c')};
  for (int i = 0; i < 80; i++) { // More than MAX_WRITE_QUEUE_BYTES
    event_bus->fire_event(immer::box<events::DockerContainerCreated>(big_event));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Let the server queue them up
  // Fail instead of hanging if the server never drops us (keepalives would keep a blocking read going forever)
  client.non_blocking(true);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  boost::system::error_code ec;
  std::size_t drained = 0;
  while (ec != boost::asio::error::eof && std::chrono::steady_clock::now() < deadline) {
    pollfd fd = {.fd = client.native_handle(), .events = POLLIN, .revents = 0};
    if (poll(&fd, 1, 100) > 0) {
      drained += client.read_some(boost::asio::buffer(chunk), ec);
    }
  }
  REQUIRE(ec == boost::asio::error::eof);
  REQUIRE(drained < 80 * big_event.container_id.size());
}
