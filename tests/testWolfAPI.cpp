#include <api/api.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <curl/curl.h>
#include <helpers/tsqueue.hpp>
#include <state/config.hpp>

using Catch::Matchers::Equals;

using namespace wolf::api;
using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

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
    logs::log(logs::trace, "[CURL] Received {} - {}", response_code, read_buf);
    return {{response_code, read_buf}};
  }
}

TEST_CASE("Pair APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  // Start the server
  std::thread server_thread([app_state]() { wolf::api::start_server(app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, "/tmp/wolf.sock");
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/pair/pending");
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true,\"requests\":[]}"));

  // Checkout the list of paired clients (there will be one in the test config file)
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/clients");
  REQUIRE(response);
  REQUIRE_THAT(response->second,
               Equals("{\"success\":true,\"clients\":[{\"client_id\":10594003729173467913,\"app_state_folder\":\"some/"
                      "folder\"}]}"));

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
               Equals("{\"success\":true,\"requests\":[{\"pair_secret\":\"secret\",\"pin\":\"1234\"}]}"));

  // Let's complete the pairing process
  response = req(curl.get(),
                 HTTPMethod::POST,
                 "http://localhost/api/v1/pair/client",
                 "{\"pair_secret\":\"secret\",\"pin\":\"1234\"}");
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));
  REQUIRE(pair_promise->get_future().get() == "1234");
}

TEST_CASE("APPs APIs", "[API]") {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  auto app_state = immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .running_sessions = running_sessions});

  // Start the server
  std::thread server_thread([app_state]() { wolf::api::start_server(app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, "/tmp/wolf.sock");
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
  REQUIRE(app_state->config->apps->load()->at(2)->base.title == "Test app");

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

  // Start the server
  std::thread server_thread([app_state]() { wolf::api::start_server(app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, "/tmp/wolf.sock");
  curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  // Test that the initial list of sessions is empty
  auto response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  REQUIRE(response);
  auto sessions = rfl::json::read<StreamSessionListResponse>(response->second).value();
  REQUIRE(sessions.success);
  REQUIRE(sessions.sessions.size() == 0);

  // Test that we can add a session
  auto session = rfl::Reflector<wolf::core::events::StreamSession>::ReflType{
      .app_id = "1",                       // test cfg file
      .client_id = "10594003729173467913", // test cfg file
      .client_ip = "127.0.0.1"};
  response = req(curl.get(), HTTPMethod::POST, "http://localhost/api/v1/sessions/add", rfl::json::write(session));
  REQUIRE(response);
  REQUIRE_THAT(response->second, Equals("{\"success\":true}"));

  // Test that the new session is in the list
  response = req(curl.get(), HTTPMethod::GET, "http://localhost/api/v1/sessions");
  REQUIRE(response);
  auto sessions2 = rfl::json::read<StreamSessionListResponse>(response->second).value();
  REQUIRE(sessions2.success);
  REQUIRE(sessions2.sessions.size() == 1);

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

  // Start the server
  std::thread server_thread([app_state]() { wolf::api::start_server(app_state); });
  server_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the server to start

  auto curl = curl_ptr(curl_easy_init(), ::curl_easy_cleanup);

  curl_easy_setopt(curl.get(), CURLOPT_UNIX_SOCKET_PATH, "/tmp/wolf.sock");

  auto queue = std::make_shared<TSQueue<SSEEvent>>();

  std::thread sse_thread(listen_sse, curl.get(), queue, "http://localhost/api/v1/events");
  sse_thread.detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(42)); // Wait for the SSE client to start

  // Test out one of the events
  event_bus->fire_event(events::IDRRequestEvent{.session_id = 42});

  auto event = queue->pop();
  REQUIRE(event.has_value());
  REQUIRE_THAT(event->event, Equals("wolf::core::events::IDRRequestEvent"));
  REQUIRE_THAT(event->data, Equals("{\"session_id\":42}"));
}