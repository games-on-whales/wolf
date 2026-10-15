#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

#include <control/control.hpp>
#include <moonlight/control.hpp>
#include <mutex>
#include <streaming/streaming.hpp>
#include <thread>
using namespace moonlight::control;
using namespace std::chrono_literals;

static std::string to_string(const ControlEncryptedPacket &packet) {
  return {(char *)&packet, packet.full_size()};
}

TEST_CASE("Control AES Encryption", "CONTROL") {
  // A bunch of packets taken from a real session

  std::string aes_key = "EDF04A215C4FBEA20934120C8480D855";

  SECTION("30 bytes") { // original packet: 01001A0000000000BF0EB6DA10E47C702EC8644EB87D9CF7B6FAC9FF75CA
    std::string payload = crypto::hex_to_str("020302000000");
    std::uint32_t seq = 0;
    auto encrypted_packet = *encrypt_packet(aes_key, seq, payload);
    REQUIRE_THAT(crypto::str_to_hex(to_string(encrypted_packet)),
                 Equals("01001A0000000000BF0EB6DA10E47C702EC8644EB87D9CF7B6FAC9FF75CA"));
    REQUIRE(boost::endian::little_to_native(encrypted_packet.seq) == seq);
    REQUIRE(boost::endian::little_to_native(encrypted_packet.header.length) ==
            sizeof(encrypted_packet.seq) + GCM_TAG_SIZE + payload.length());

    auto decrypted = decrypt_packet(encrypted_packet, aes_key);
    REQUIRE_THAT(decrypted, Equals(payload));
    REQUIRE_THAT(packet_type_to_str(((ControlPacket *)decrypted.data())->type), Equals("IDR_FRAME"));
  }

  SECTION("29 bytes") { // original packet: 010019000100000021DBB8DC0590AF3A2B20BCE5A347DE31D366E5B9C5"
    std::string payload = crypto::hex_to_str("0703010000");
    std::uint32_t seq = 1;
    auto encrypted_packet = *encrypt_packet(aes_key, seq, payload);
    REQUIRE_THAT(crypto::str_to_hex(to_string(encrypted_packet)),
                 Equals("010019000100000021DBB8DC0590AF3A2B20BCE5A347DE31D366E5B9C5"));
    REQUIRE(boost::endian::little_to_native(encrypted_packet.seq) == seq);
    REQUIRE(boost::endian::little_to_native(encrypted_packet.header.length) ==
            sizeof(encrypted_packet.seq) + GCM_TAG_SIZE + payload.length());

    auto decrypted = decrypt_packet(encrypted_packet, aes_key);
    REQUIRE_THAT(decrypted, Equals(payload));
    REQUIRE_THAT(packet_type_to_str(((ControlPacket *)decrypted.data())->type), Equals("START_B"));
  }

  SECTION("36 bytes") { // original packet: 0100200002000000220722FBADED58A03F2E8898F0F1DCB7C93F6235590618E4186AD990
    std::string payload = crypto::hex_to_str("000208000400000000000000");
    std::uint32_t seq = 2;
    auto encrypted_packet = *encrypt_packet(aes_key, seq, payload);
    REQUIRE_THAT(crypto::str_to_hex(to_string(encrypted_packet)),
                 Equals("0100200002000000220722FBADED58A03F2E8898F0F1DCB7C93F6235590618E4186AD990"));
    REQUIRE(boost::endian::little_to_native(encrypted_packet.seq) == seq);
    REQUIRE(boost::endian::little_to_native(encrypted_packet.header.length) ==
            sizeof(encrypted_packet.seq) + GCM_TAG_SIZE + payload.length());

    auto decrypted = decrypt_packet(encrypted_packet, aes_key);
    REQUIRE_THAT(decrypted, Equals(payload));
    REQUIRE_THAT(packet_type_to_str(((ControlPacket *)decrypted.data())->type), Equals("PERIODIC_PING"));
  }

  SECTION("46 bytes") { // original packet:
                        // 01002A00060000005A4D999FB2542F85BDD39D99F77EB825254569D2C04E21241B5CEC01BD3F93129718ECC1F153
    std::string payload = crypto::hex_to_str("060212000000000E05000000033400C00000059F0329");
    std::uint32_t seq = 6;
    auto encrypted_packet = *encrypt_packet(aes_key, seq, payload);
    REQUIRE_THAT(
        crypto::str_to_hex(to_string(encrypted_packet)),
        Equals("01002A00060000005A4D999FB2542F85BDD39D99F77EB825254569D2C04E21241B5CEC01BD3F93129718ECC1F153"));
    REQUIRE(boost::endian::little_to_native(encrypted_packet.seq) == seq);
    REQUIRE(boost::endian::little_to_native(encrypted_packet.header.length) ==
            sizeof(encrypted_packet.seq) + GCM_TAG_SIZE + payload.length());

    auto decrypted = decrypt_packet(encrypted_packet, aes_key);
    REQUIRE_THAT(decrypted, Equals(payload));
    REQUIRE_THAT(packet_type_to_str(((ControlPacket *)decrypted.data())->type), Equals("INPUT_DATA"));
  }
}

TEST_CASE("control joypad input packets") {
  std::string payload =
      crypto::hex_to_str("060222000000001E0C0000001A000000010014000010000000000000000000009C0000005500");

  auto input_data = (pkts::CONTROLLER_MULTI_PACKET *)payload.data();
  auto pressed_btns = input_data->button_flags | (input_data->buttonFlags2 << 16);

  REQUIRE(input_data->type == pkts::CONTROLLER_MULTI);
  REQUIRE(input_data->active_gamepad_mask == 1);
  REQUIRE(pressed_btns & pkts::CONTROLLER_BTN::A);
}
TEST_CASE("control: a resumed stream supersedes the dead peer of the same session") {
  ENetPeer dead_peer{}, resumed_peer{}, other_client_peer{};
  auto session = [](std::size_t id) {
    return immer::box<wolf::core::events::StreamSession>(wolf::core::events::StreamSession{.session_id = id});
  };

  control::enet_clients_map clients;
  clients = clients.set(&dead_peer, session(42)).set(&other_client_peer, session(7));

  // The resumed peer connects for session 42 while the dead one is still waiting to time out
  auto stale = control::superseded_peers(clients, &resumed_peer, 42);
  REQUIRE(stale.size() == 1);
  REQUIRE(stale[0] == &dead_peer);

  // Other clients' peers are never touched
  REQUIRE(control::superseded_peers(clients, &resumed_peer, 7).size() == 1);
  REQUIRE(control::superseded_peers(clients, &other_client_peer, 7).empty());

  // Re-registering the same peer doesn't supersede itself
  REQUIRE(control::superseded_peers(clients, &dead_peer, 42).empty());
}

TEST_CASE("control: a pause from a stream replaced by /resume is ignored") {
  using wolf::core::events::StreamSession;
  StreamSession dead_stream{.session_id = 42};
  dead_stream.enet_secret_payload = 1;
  StreamSession resumed_stream = dead_stream;
  resumed_stream.enet_secret_payload = 2;
  StreamSession other_client{.session_id = 7};
  other_client.enet_secret_payload = 1;

  // Before the resume the dead stream is still the current one: its pause is real
  REQUIRE_FALSE(control::is_superseded_stream(immer::vector<StreamSession>{dead_stream, other_client}, dead_stream));

  // After the resume replaced it, the dead peer's late pause must be ignored, the new peer's must not
  immer::vector<StreamSession> running{other_client, resumed_stream};
  REQUIRE(control::is_superseded_stream(running, dead_stream));
  REQUIRE_FALSE(control::is_superseded_stream(running, resumed_stream));

  // Session already gone (e.g. cancelled): keep pausing as before
  REQUIRE_FALSE(control::is_superseded_stream(immer::vector<StreamSession>{other_client}, dead_stream));
}

namespace {
/* A Moonlight-like ENet client: connects with the session's ENet secret as connect data */
struct TestENetClient {
  ENetHost *host = enet_host_create(AF_INET, nullptr, 1, 1, 0, 0);
  ENetPeer *peer = nullptr;
  bool disconnected = false;
  std::size_t received = 0;

  TestENetClient(int port, std::uint32_t enet_secret) {
    ENetAddress addr;
    enet_address_set_host(&addr, "127.0.0.1");
    enet_address_set_port(&addr, port);
    peer = enet_host_connect(host, &addr, 1, enet_secret);
    ENetEvent ev;
    for (int i = 0; i < 200; i++) {
      if (enet_host_service(host, &ev, 10) > 0 && ev.type == ENET_EVENT_TYPE_CONNECT)
        return;
    }
    FAIL("ENet client failed to connect");
  }

  void service(std::chrono::milliseconds duration) {
    auto end = std::chrono::steady_clock::now() + duration;
    ENetEvent ev;
    while (std::chrono::steady_clock::now() < end) {
      while (enet_host_service(host, &ev, 5) > 0) {
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
          disconnected = true;
        if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
          received++;
          enet_packet_destroy(ev.packet);
        }
      }
    }
  }

  void send_termination(const std::string &aes_key) {
    auto pkt = ControlTerminatePacket{};
    auto encrypted = encrypt_packet(aes_key, 0, {(char *)&pkt, sizeof(pkt)});
    enet_peer_send(peer, 0, enet_packet_create(encrypted.get(), encrypted->full_size(), ENET_PACKET_FLAG_RELIABLE));
    enet_host_flush(host);
  }

  ~TestENetClient() {
    enet_host_destroy(host);
  }
};
} // namespace

TEST_CASE("control: a dead client's late pause doesn't end its resumed stream", "[ENET]") {
  using wolf::core::events::StreamSession;
  constexpr int port = 47998;
  const std::string old_key = "EDF04A215C4FBEA20934120C8480D855", new_key = "00112233445566778899AABBCCDDEEFF";
  auto make_session = [](std::uint32_t enet_secret, const std::string &aes_key) {
    StreamSession session{.session_id = 42};
    session.enet_secret_payload = enet_secret;
    session.rtp_secret_payload = {static_cast<char>(enet_secret)};
    session.aes_key = aes_key;
    session.ip = "10.255.255.1"; // never matches loopback, keep the IP fallback out of the way
    return session;
  };

  auto event_bus = std::make_shared<wolf::core::events::EventBusType>();
  std::atomic<int> pauses = 0;
  auto pause_handler = event_bus->register_handler<immer::box<wolf::core::events::PauseStreamEvent>>(
      [&pauses](const immer::box<wolf::core::events::PauseStreamEvent> &ev) { pauses++; });
  // The RTP secret (first byte) of each stream instance ended as superseded
  std::mutex ended_mutex;
  std::vector<char> ended_streams;
  auto end_handler = event_bus->register_handler<immer::box<wolf::core::events::EndSupersededStreamEvent>>(
      [&](const immer::box<wolf::core::events::EndSupersededStreamEvent> &ev) {
        std::lock_guard lock(ended_mutex);
        ended_streams.push_back(ev->rtp_secret_payload[0]);
      });
  auto ended = [&]() {
    std::lock_guard lock(ended_mutex);
    return ended_streams;
  };
  state::SessionsAtoms running_sessions =
      std::make_shared<immer::atom<immer::vector<StreamSession>>>(immer::vector{make_session(1, old_key)});

  REQUIRE(control::init());
  std::thread([=] { control::run_control(port, running_sessions, event_bus, 20, 10ms, "127.0.0.1"); }).detach();
  std::this_thread::sleep_for(200ms);

  TestENetClient dead_client(port, 1);
  dead_client.service(100ms);

  // /resume replaces the running session: same session_id, new ENet secret and AES key
  running_sessions->store(immer::vector{make_session(2, new_key)});

  // The dead client's pause raised before the resumed client connects only ends its own stream instance
  dead_client.send_termination(old_key);
  dead_client.service(200ms);
  REQUIRE(pauses == 0);
  REQUIRE(ended() == std::vector<char>{1});

  // Once the resumed client connects the dead peer is dropped, its late disconnect can't fire anything
  TestENetClient resumed_client(port, 2);
  resumed_client.service(100ms); // ack the handshake so that the server sees the connection
  dead_client.service(200ms);
  REQUIRE(dead_client.disconnected);
  REQUIRE(pauses == 0);
  REQUIRE(ended() == std::vector<char>{1, 1});

  // Stopping the stream reaches the resumed client
  event_bus->fire_event(
      immer::box<wolf::core::events::StopStreamEvent>(wolf::core::events::StopStreamEvent{.session_id = 42}));
  resumed_client.service(200ms);
  REQUIRE(resumed_client.received == 1);

  // A real pause from the current client still goes through
  resumed_client.send_termination(new_key);
  resumed_client.service(200ms);
  REQUIRE(pauses == 1);
  REQUIRE(ended() == std::vector<char>{1, 1});

  // The same stream instance reconnecting its ENet peer keeps its pipelines
  TestENetClient reconnected_client(port, 2);
  reconnected_client.service(100ms);
  resumed_client.service(200ms);
  REQUIRE(resumed_client.disconnected);
  REQUIRE(ended() == std::vector<char>{1, 1});
}

TEST_CASE("streaming: a superseded stream instance ends only its own pipelines", "[GSTREAMER]") {
  using namespace wolf::core;
  auto event_bus = std::make_shared<events::EventBusType>();
  boost::asio::io_context io;
  auto rtp_secret = [](char id) { return std::array<char, 16>{id}; };

  // Runs a test pipeline in its own thread; returns a flag that's set once it has ended
  auto start_video = [&](std::size_t session_id, char secret) {
    events::VideoSession video{.gst_pipeline = "videotestsrc is-live=true ! fakesink", .session_id = session_id};
    video.display_mode = {.width = 64, .height = 64, .refreshRate = 30};
    video.bitrate_kbps = 1000;
    video.packet_size = 1024;
    video.rtp_secret_payload = rtp_secret(secret);
    auto ended = std::make_shared<std::atomic_bool>(false);
    std::thread([=, &io] {
      streaming::start_streaming_video(video,
                                       event_bus,
                                       "127.0.0.1",
                                       9,
                                       std::make_shared<immer::atom<gst_video_context::gst_context_ptr>>(),
                                       std::make_shared<boost::asio::ip::udp::socket>(io, boost::asio::ip::udp::v4()));
      *ended = true;
    }).detach();
    return ended;
  };
  auto start_audio = [&](std::size_t session_id, char secret) {
    events::AudioSession audio{.gst_pipeline = "audiotestsrc is-live=true ! fakesink", .session_id = session_id};
    audio.rtp_secret_payload = rtp_secret(secret);
    auto ended = std::make_shared<std::atomic_bool>(false);
    std::thread([=, &io] {
      streaming::start_streaming_audio(audio,
                                       event_bus,
                                       "127.0.0.1",
                                       9,
                                       std::make_shared<boost::asio::ip::udp::socket>(io, boost::asio::ip::udp::v4()),
                                       "sink",
                                       "");
      *ended = true;
    }).detach();
    return ended;
  };

  auto old_video = start_video(42, 1), old_audio = start_audio(42, 1);
  auto resumed_video = start_video(42, 2), resumed_audio = start_audio(42, 2);
  auto other_session_video = start_video(7, 1);
  std::this_thread::sleep_for(700ms);

  event_bus->fire_event(immer::box<events::EndSupersededStreamEvent>(
      events::EndSupersededStreamEvent{.session_id = 42, .rtp_secret_payload = rtp_secret(1)}));
  std::this_thread::sleep_for(1500ms);
  CHECK(*old_video);
  CHECK(*old_audio);
  CHECK_FALSE(*resumed_video);
  CHECK_FALSE(*resumed_audio);
  CHECK_FALSE(*other_session_video);

  for (std::size_t session_id : {42, 7}) {
    event_bus->fire_event(immer::box<events::StopStreamEvent>(events::StopStreamEvent{.session_id = session_id}));
  }
  std::this_thread::sleep_for(1500ms);
  CHECK(*resumed_video);
  CHECK(*resumed_audio);
  CHECK(*other_session_video);
}
