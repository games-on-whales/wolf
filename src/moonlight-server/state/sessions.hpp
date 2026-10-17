#pragma once

#include <boost/asio/ip/address.hpp>
#include <crypto/crypto.hpp>
#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <immer/vector.hpp>
#include <optional>
#include <range/v3/view.hpp>
#include <state/config.hpp>
#include <state/serialised_config.hpp>

namespace state {

using namespace wolf::core;

inline std::optional<events::StreamSession> get_session_by_id(const immer::vector<events::StreamSession> &sessions,
                                                              const std::size_t id) {
  auto results =
      sessions |                                                                                             //
      ranges::views::filter([id](const events::StreamSession &session) { return session.session_id == id; }) //
      | ranges::views::take(1)                                                                               //
      | ranges::to_vector;                                                                                   //
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple sessions for a given ID: {}", id);
    return {};
  }
}

inline std::optional<events::StreamSession> get_session_by_client(const immer::vector<events::StreamSession> &sessions,
                                                                  const wolf::config::PairedClient &client) {
  auto client_id = get_client_id(client);
  return get_session_by_id(sessions, client_id);
}

inline std::optional<events::Lobby> get_lobby_by_id(const immer::vector<events::Lobby> &lobbies,
                                                    std::string_view lobby_id) {
  auto results = lobbies |                                                                                      //
                 ranges::views::filter([lobby_id](const events::Lobby &lobby) { return lobby.id == lobby_id; }) //
                 | ranges::views::take(1)                                                                       //
                 | ranges::to_vector;                                                                           //
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple lobbies for a given ID: {}", lobby_id);
    return {};
  }
}

inline std::optional<events::Lobby> get_lobby_by_connected_session(const immer::vector<events::Lobby> &lobbies,
                                                                   std::string_view session_id) {
  for (const events::Lobby &lobby : lobbies) {
    immer::vector<immer::box<std::string>> sessions = lobby.connected_sessions->load();
    auto session = std::find_if(sessions.begin(), sessions.end(), [session_id](const auto &session) {
      return session == session_id;
    });
    if (session == sessions.end()) {
      continue;
    }
    return lobby;
  }
  return {};
}

/**
 * True for loopback, private (RFC 1918), CGNAT, link-local and unique local addresses
 */
inline bool is_lan_address(const std::string &ip) {
  boost::system::error_code ec;
  auto address = boost::asio::ip::make_address(ip, ec);
  if (ec) {
    return false;
  }
  if (address.is_v6() && address.to_v6().is_v4_mapped()) {
    address = boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, address.to_v6());
  }
  if (address.is_v4()) {
    auto v = address.to_v4().to_uint();
    auto in = [v](std::uint32_t net, int bits) { return (v >> (32 - bits)) == (net >> (32 - bits)); };
    return in(0x7F000000, 8) || in(0x0A000000, 8) || in(0xAC100000, 12) || in(0xC0A80000, 16) || in(0x64400000, 10) ||
           in(0xA9FE0000, 16);
  }
  auto v6 = address.to_v6();
  return v6.is_loopback() || v6.is_link_local() || (v6.to_bytes()[0] & 0xFE) == 0xFC;
}

/**
 * The encryption policy for a client, from WOLF_LAN_ENCRYPTION_MODE and WOLF_WAN_ENCRYPTION_MODE
 * (disabled|optional|required, or Sunshine's 0|1|2). Defaults: disabled on the LAN, optional elsewhere.
 */
inline moonlight::encryption::Mode encryption_mode_for(const std::string &client_ip) {
  using moonlight::encryption::Mode;
  if (is_lan_address(client_ip)) {
    return moonlight::encryption::parse_mode(utils::get_env("WOLF_LAN_ENCRYPTION_MODE", "disabled"), Mode::DISABLED);
  }
  return moonlight::encryption::parse_mode(utils::get_env("WOLF_WAN_ENCRYPTION_MODE", "optional"), Mode::OPTIONAL);
}

inline std::shared_ptr<events::StreamSession>
create_stream_session(immer::box<state::AppState> state,
                      const events::App &run_app,
                      const wolf::config::PairedClient &current_client,
                      const moonlight::DisplayMode &display_mode,
                      int audio_channel_count,
                      const std::string &aes_key,
                      const std::string &aes_iv,
                      moonlight::encryption::Mode encryption_mode = moonlight::encryption::Mode::DISABLED,
                      int client_core_version = -1) {
  auto full_path = std::filesystem::path(state->host->local_base_state_folder) / current_client.app_state_folder /
                   run_app.base.title;
  logs::log(logs::debug, "Host app state folder: {}, creating paths", full_path.string());
  std::filesystem::create_directories(full_path);

  std::random_device rd;
  std::mt19937 generator(rd());

  std::uniform_int_distribution<> chars(33, 126); // ASCII values for printable character
  std::array<char, 16> rtp_secret_payload;
  for (auto &c : rtp_secret_payload) {
    c = static_cast<char>(chars(generator));
  }

  std::uniform_int_distribution<u_int32_t> uints(0, UINT32_MAX);

  std::uniform_int_distribution<> ints(0, 255);
  auto rtsp_fake_ip = fmt::format("{}.{}.{}.{}", ints(generator), ints(generator), ints(generator), ints(generator));

  auto session = events::StreamSession{
      .display_mode = display_mode,
      .audio_channel_count = audio_channel_count,
      .event_bus = state->event_bus,
      .client_settings = current_client.settings,
      .app = std::make_shared<events::App>(run_app),
      .app_local_state_folder = full_path.string(),
      .app_host_state_folder = std::filesystem::path(state->host->host_base_state_folder) /
                               current_client.app_state_folder / run_app.base.title,

      .aes_key = aes_key,
      .aes_iv = aes_iv,

      // Moonlight protocol extension to support IP-less connections
      .rtp_secret_payload = rtp_secret_payload,
      .enet_secret_payload = uints(generator),
      .rtsp_fake_ip = rtsp_fake_ip,
      .encryption = std::make_shared<events::StreamEncryption>(crypto::hex_to_str(aes_key, true),
                                                               encryption_mode,
                                                               client_core_version),

      // client info
      .session_id = get_client_id(current_client),
      .video_stream_port = static_cast<unsigned short>(get_port(VIDEO_PING_PORT)),
      .audio_stream_port = static_cast<unsigned short>(get_port(AUDIO_PING_PORT)),
      .control_stream_port = static_cast<unsigned short>(get_port(CONTROL_PORT))};

  return std::make_shared<events::StreamSession>(session);
}

inline immer::vector<events::StreamSession> remove_session(const immer::vector<events::StreamSession> &sessions,
                                                           const events::StreamSession &session) {
  return sessions                                                                                           //
         | ranges::views::filter([remove_hash = session.session_id](const events::StreamSession &cur_ses) { //
             return cur_ses.session_id != remove_hash;                                                      //
           })                                                                                               //
         | ranges::to<immer::vector<events::StreamSession>>();                                              //
}
} // namespace state