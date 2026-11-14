#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <sessions/common.hpp>
#include <sessions/handlers.hpp>
#include <state/config.hpp>
#include <thread>

using namespace wolf::core;
using namespace std::chrono_literals;

static constexpr auto TEST_TIMEOUT = 50ms;

/**
 * Polls `done` until it returns true or `limit` has passed
 */
static bool wait_for(const std::function<bool()> &done, std::chrono::milliseconds limit = 2s) {
  auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return done();
}

/**
 * An AppState with one running Moonlight session and no lobbies
 */
static immer::box<state::AppState> make_app_state(std::size_t session_id) {
  auto event_bus = std::make_shared<events::EventBusType>();
  auto running_sessions = std::make_shared<immer::atom<immer::vector<events::StreamSession>>>();
  auto config = immer::box<state::Config>(state::load_or_default("config.test.toml", event_bus, running_sessions));
  running_sessions->update([&](const immer::vector<events::StreamSession> &sessions) {
    return sessions.push_back(events::StreamSession{.session_id = session_id});
  });
  return immer::box<state::AppState>(state::AppState{
      .config = config,
      .pairing_cache = std::make_shared<immer::atom<immer::map<std::string, state::PairCache>>>(),
      .pairing_atom = std::make_shared<immer::atom<immer::map<std::string, immer::box<events::PairSignal>>>>(),
      .event_bus = event_bus,
      .lobbies = std::make_shared<immer::atom<immer::vector<events::Lobby>>>(),
      .running_sessions = running_sessions});
}

TEST_CASE("Opt-in timeouts are read from the environment", "[Sessions]") {
  const char *env_var = "WOLF_TEST_OPT_IN_TIMEOUT_MS";

  unsetenv(env_var);
  REQUIRE(sessions::get_opt_in_timeout(env_var) == 0ms);

  setenv(env_var, "1500", 1);
  REQUIRE(sessions::get_opt_in_timeout(env_var) == 1500ms);

  setenv(env_var, "-1", 1);
  REQUIRE(sessions::get_opt_in_timeout(env_var) == 0ms);

  setenv(env_var, "soon", 1);
  REQUIRE(sessions::get_opt_in_timeout(env_var) == 0ms);

  unsetenv(env_var);
}

TEST_CASE("Paused sessions are stopped after a timeout", "[Sessions]") {
  const std::size_t session_id = 1234;
  auto app_state = make_app_state(session_id);
  auto event_bus = app_state->event_bus;

  std::atomic<int> stops = 0;
  std::atomic<std::chrono::steady_clock::time_point> stopped_at{};
  auto observer = event_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [&stops, &stopped_at, session_id](const immer::box<events::StopStreamEvent> &ev) {
        if (ev->session_id == session_id) {
          stopped_at = std::chrono::steady_clock::now();
          stops++;
        }
      });

  auto pause = [&]() {
    event_bus->fire_event(immer::box<events::PauseStreamEvent>(events::PauseStreamEvent{.session_id = session_id}));
  };
  auto resume = [&]() {
    event_bus->fire_event(immer::box<events::ResumeStreamEvent>(events::ResumeStreamEvent{.session_id = session_id}));
  };
  auto stop = [&]() {
    event_bus->fire_event(immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = session_id}));
  };

  SECTION("Pause, then Stop once the timeout has passed") {
    auto handlers = sessions::setup_paused_session_handlers(app_state, TEST_TIMEOUT);
    pause();
    REQUIRE(stops.load() == 0);
    REQUIRE(wait_for([&]() { return stops.load() == 1; }));
  }

  SECTION("Resume before the timeout cancels the Stop") {
    auto handlers = sessions::setup_paused_session_handlers(app_state, TEST_TIMEOUT);
    pause();
    resume();
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 0);
  }

  SECTION("Pausing again after a Resume restarts the timeout") {
    auto timeout = TEST_TIMEOUT * 4;
    auto handlers = sessions::setup_paused_session_handlers(app_state, timeout);
    pause();
    resume();
    std::this_thread::sleep_for(timeout / 2);
    auto paused_again = std::chrono::steady_clock::now();
    pause();
    REQUIRE(wait_for([&]() { return stops.load() == 1; }));
    REQUIRE(stopped_at.load() - paused_again >= timeout);
  }

  SECTION("Pausing twice stops the session once") {
    auto handlers = sessions::setup_paused_session_handlers(app_state, TEST_TIMEOUT);
    pause();
    pause();
    REQUIRE(wait_for([&]() { return stops.load() == 1; }));
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 1);
  }

  SECTION("Stop before the timeout cancels the pending Stop") {
    auto handlers = sessions::setup_paused_session_handlers(app_state, TEST_TIMEOUT);
    pause();
    stop();
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 1);
  }

  SECTION("A Pause after the session has stopped arms nothing") {
    auto handlers = sessions::setup_paused_session_handlers(app_state, TEST_TIMEOUT);
    app_state->running_sessions->update([](const immer::vector<events::StreamSession> &) {
      return immer::vector<events::StreamSession>{};
    });
    stop();
    pause();
    // The same client starts a new session under the same id
    app_state->running_sessions->update([&](const immer::vector<events::StreamSession> &sessions) {
      return sessions.push_back(events::StreamSession{.session_id = session_id});
    });
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 1);
  }

  SECTION("A zero timeout arms nothing") {
    auto handlers = sessions::setup_paused_session_handlers(app_state, 0ms);
    REQUIRE(handlers.empty());
    pause();
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 0);
  }
}

TEST_CASE("Empty lobbies are stopped after a timeout", "[Sessions]") {
  const std::size_t session_id = 1234;
  auto app_state = make_app_state(session_id);
  auto event_bus = app_state->event_bus;

  // A persistent lobby with one connected Moonlight session, added straight to state (no compositor).
  const std::string lobby_id = "lobby-under-test";
  events::Lobby lobby{.id = lobby_id,
                      .name = "test_lobby",
                      .started_by_profile_id = "test_profile",
                      .multi_user = false,
                      .stop_when_everyone_leaves = false};
  lobby.connected_sessions->update([&](const immer::vector<immer::box<std::string>> &connected) {
    return connected.push_back(immer::box<std::string>{std::to_string(session_id)});
  });
  app_state->lobbies->update([&](const immer::vector<events::Lobby> &lobbies) { return lobbies.push_back(lobby); });

  auto lobbies_handlers = sessions::setup_lobbies_handlers(app_state, utils::get_env("XDG_RUNTIME_DIR", "/tmp/"), {});

  std::atomic<int> stops = 0;
  std::atomic<std::chrono::steady_clock::time_point> stopped_at{};
  auto observer = event_bus->register_handler<immer::box<events::StopLobbyEvent>>(
      [&stops, &stopped_at, lobby_id](const immer::box<events::StopLobbyEvent> &ev) {
        if (ev->lobby_id == lobby_id) {
          stopped_at = std::chrono::steady_clock::now();
          stops++;
        }
      });

  auto leave = [&]() {
    auto ev = events::LeaveLobbyEvent{.lobby_id = lobby_id, .moonlight_session_id = session_id};
    event_bus->fire_event(immer::box<events::LeaveLobbyEvent>(ev));
  };
  auto join = [&]() {
    auto ev = events::JoinLobbyEvent{.lobby_id = lobby_id, .moonlight_session_id = session_id};
    event_bus->fire_event(immer::box<events::JoinLobbyEvent>(ev));
  };

  SECTION("Leave, then StopLobby once the timeout has passed") {
    auto handlers = sessions::setup_empty_lobby_handlers(app_state, TEST_TIMEOUT);
    leave();
    REQUIRE(stops.load() == 0);
    REQUIRE(wait_for([&]() { return stops.load() == 1 && app_state->lobbies->load()->empty(); }));
  }

  SECTION("Joining again before the timeout keeps the lobby") {
    auto handlers = sessions::setup_empty_lobby_handlers(app_state, TEST_TIMEOUT);
    leave();
    join();
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 0);
    REQUIRE(app_state->lobbies->load()->size() == 1);
  }

  SECTION("Leaving again after a join restarts the timeout") {
    auto timeout = TEST_TIMEOUT * 4;
    auto handlers = sessions::setup_empty_lobby_handlers(app_state, timeout);
    leave();
    join();
    std::this_thread::sleep_for(timeout / 2);
    auto left_again = std::chrono::steady_clock::now();
    leave();
    REQUIRE(wait_for([&]() { return stops.load() == 1; }));
    REQUIRE(stopped_at.load() - left_again >= timeout);
  }

  SECTION("A zero timeout arms nothing") {
    auto handlers = sessions::setup_empty_lobby_handlers(app_state, 0ms);
    REQUIRE(handlers.empty());
    leave();
    std::this_thread::sleep_for(TEST_TIMEOUT * 4);
    REQUIRE(stops.load() == 0);
  }
}
