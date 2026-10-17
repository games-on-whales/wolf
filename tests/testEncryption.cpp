#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

#include <boost/asio.hpp>
#include <crypto/crypto.hpp>
#include <gst-plugin/video.hpp>
#include <moonlight/control.hpp>
#include <moonlight/encryption.hpp>
#include <moonlight/fec.hpp>
#include <moonlight/protocol.hpp>
#include <set>
#include <state/sessions.hpp>
#include <thread>

using namespace std::string_literals;
using namespace moonlight;

#include "encryption_client.hpp"

TEST_CASE("Reusable AES-GCM context", "[Encryption]") {
  crypto::aes_gcm gcm(KEY);
  auto iv = client_iv(42, 'H', 'V');
  std::string plaintext = "a video packet, or any other message";
  std::string sealed(plaintext.size(), '\0');
  std::string tag(16, '\0');
  gcm.seal(iv, plaintext, (std::uint8_t *)sealed.data(), (std::uint8_t *)tag.data());

  // Same bytes as the one-shot helper
  auto [expected, expected_tag] = crypto::aes_encrypt_gcm(plaintext, KEY, iv, 12);
  REQUIRE(sealed == expected);
  REQUIRE(tag == expected_tag);

  std::string opened(sealed.size(), '\0');
  REQUIRE(gcm.open(iv, sealed, tag, (std::uint8_t *)opened.data()));
  REQUIRE(opened == plaintext);

  // Reusing the context for another message doesn't leak state between them
  gcm.seal(client_iv(43, 'H', 'V'), plaintext, (std::uint8_t *)sealed.data(), (std::uint8_t *)tag.data());
  REQUIRE(sealed != expected);

  sealed[3] ^= 1;
  REQUIRE_FALSE(gcm.open(client_iv(43, 'H', 'V'), sealed, tag, (std::uint8_t *)opened.data()));
  REQUIRE_THROWS(crypto::aes_gcm("short key"));
}

TEST_CASE("Encrypted RTSP framing", "[Encryption]") {
  crypto::aes_gcm gcm(KEY);
  auto request = "DESCRIBE rtsp://10.0.0.1:48010 RTSP/1.0\r\nCSeq: 2\r\n\r\n"s;

  SECTION("Client messages open") {
    auto sealed = client_seal_rtsp(7, request);
    REQUIRE(encryption::is_encrypted_rtsp(sealed));
    REQUIRE(encryption::encrypted_rtsp_size(sealed) == sealed.size());
    auto opened = encryption::open_rtsp(gcm, sealed);
    REQUIRE(opened);
    REQUIRE(opened->seq == 7);
    REQUIRE(opened->plaintext == request);
  }

  SECTION("Host replies open in the client") {
    auto sealed = encryption::seal_rtsp(gcm, 7, "RTSP/1.0 200 OK\r\nCSeq: 2\r\n\r\n");
    REQUIRE_THAT(client_open_rtsp(sealed), Equals("RTSP/1.0 200 OK\r\nCSeq: 2\r\n\r\n"));
  }

  SECTION("Rejects tampering, other keys, truncation, padding and plaintext") {
    auto sealed = client_seal_rtsp(7, request);
    auto tampered = sealed;
    tampered[30] ^= 1;
    REQUIRE_FALSE(encryption::open_rtsp(gcm, tampered));

    auto other_key = crypto::hex_to_str("00112233445566778899aabbccddeeff", true);
    REQUIRE_FALSE(encryption::open_rtsp(gcm, client_seal_rtsp(7, request, other_key)));

    REQUIRE_FALSE(encryption::open_rtsp(gcm, sealed.substr(0, sealed.size() - 1)));
    REQUIRE_FALSE(encryption::open_rtsp(gcm, sealed + "x"));
    REQUIRE_FALSE(encryption::open_rtsp(gcm, request));
    REQUIRE_FALSE(encryption::is_encrypted_rtsp(request));

    // A client message can't be passed off as a host message (different IV tag)
    REQUIRE_FALSE(encryption::open_rtsp(gcm, encryption::seal_rtsp(gcm, 7, request)));
  }
}

TEST_CASE("Replay window", "[Encryption]") {
  SECTION("Strictly increasing (RTSP)") {
    encryption::ReplayWindow<1> w;
    REQUIRE(w.accept(1));
    REQUIRE(w.accept(2));
    REQUIRE_FALSE(w.accept(2));
    REQUIRE_FALSE(w.accept(1));
    REQUIRE(w.accept(10));
    REQUIRE_FALSE(w.accept(9));
  }

  SECTION("Out of order within the window (control)") {
    encryption::ReplayWindow<64> w;
    REQUIRE(w.accept(100));
    REQUIRE(w.accept(98));
    REQUIRE_FALSE(w.accept(98));
    REQUIRE(w.accept(99));
    REQUIRE(w.accept(101));
    REQUIRE_FALSE(w.accept(100));
    REQUIRE(w.accept(200));
    REQUIRE_FALSE(w.accept(136)); // older than the window
    REQUIRE(w.accept(137));
    REQUIRE(w.accept(1000));
    REQUIRE_FALSE(w.accept(200));
  }
}

TEST_CASE("Video packet encryption", "[Encryption]") {
  crypto::aes_gcm gcm(KEY);
  std::string packet = "RTP header + NV_VIDEO_PACKET + slice of an encoded frame";
  std::string out(sizeof(encryption::EncVideoHeader) + packet.size(), '\0');
  encryption::seal_video(gcm, 5, 3, 1234, packet, (std::uint8_t *)out.data());

  // VideoStream.c: ENC_VIDEO_HEADER { iv[12], frameNumber (LE), tag[16] } then the ciphertext
  auto iv = out.substr(0, 12);
  std::uint32_t frame_number;
  std::memcpy(&frame_number, out.data() + 12, 4);
  REQUIRE(boost::endian::little_to_native(frame_number) == 1234);
  REQUIRE(iv[10] == 'H');
  REQUIRE(iv[11] == 'V');
  REQUIRE(crypto::aes_decrypt_gcm(out.substr(32), KEY, out.substr(16, 16), iv, 12) == packet);

  // IVs never repeat across packets of a pipeline or across pipelines (epochs) for the same key
  std::set<std::string> ivs;
  for (std::uint32_t epoch : {1u, 2u, 0xFFFFFFFFu}) {
    for (std::uint64_t counter :
         {std::uint64_t(0), std::uint64_t(1), std::uint64_t(1) << 32, (std::uint64_t(1) << 48) - 1}) {
      auto v = encryption::make_video_iv(counter, epoch);
      REQUIRE(ivs.insert({v.data(), v.size()}).second);
    }
  }
}

TEST_CASE("Video RTP packets are encrypted in the GStreamer plugin", "[Encryption]") {
  gst_init(nullptr, nullptr);
  moonlight::fec::init();

  auto make_pay = [](bool encrypt) {
    auto pay = (gst_rtp_moonlight_pay_video *)g_object_new(gst_TYPE_rtp_moonlight_pay_video, nullptr);
    g_object_set(pay, "payload_size", 32, "fec_percentage", 50, "add_padding", TRUE, nullptr);
    if (encrypt) {
      g_object_set(pay, "aes_key", "9d804e47a6aa6624b7d4b502b32cc522", "iv_epoch", 7u, "encrypt", TRUE, nullptr);
    }
    return pay;
  };
  auto frame = "this is a video frame that doesn't fit in a single RTP packet"s;
  auto make_buf = [&frame]() {
    auto buf = gst_buffer_new_allocate(nullptr, frame.size(), nullptr);
    gst_buffer_fill(buf, 0, frame.data(), frame.size());
    return buf;
  };

  auto plain_pay = make_pay(false);
  auto enc_pay = make_pay(true);
  auto plain_buf = make_buf();
  auto enc_buf = make_buf();
  auto plain = gst_moonlight_video::split_into_rtp(plain_pay, plain_buf);
  auto encrypted =
      gst_moonlight_video::encrypt_rtp_packets(*enc_pay, gst_moonlight_video::split_into_rtp(enc_pay, enc_buf));
  REQUIRE(encrypted != nullptr);
  REQUIRE(gst_buffer_list_length(encrypted) == gst_buffer_list_length(plain));
  REQUIRE(gst_buffer_list_length(plain) > 2); // data and FEC shards

  std::set<std::string> ivs;
  for (guint i = 0; i < gst_buffer_list_length(plain); i++) {
    GstMapInfo p, e;
    gst_buffer_map(gst_buffer_list_get(plain, i), &p, GST_MAP_READ);
    gst_buffer_map(gst_buffer_list_get(encrypted, i), &e, GST_MAP_READ);
    std::string expected((char *)p.data, p.size);
    std::string sealed((char *)e.data, e.size);
    gst_buffer_unmap(gst_buffer_list_get(plain, i), &p);
    gst_buffer_unmap(gst_buffer_list_get(encrypted, i), &e);

    REQUIRE(sealed.size() == expected.size() + 32);
    REQUIRE(ivs.insert(sealed.substr(0, 12)).second);
    REQUIRE(crypto::aes_decrypt_gcm(sealed.substr(32), KEY, sealed.substr(16, 16), sealed.substr(0, 12), 12) ==
            expected);
  }

  // Without a key nothing is produced, rather than plaintext
  auto no_key = (gst_rtp_moonlight_pay_video *)g_object_new(gst_TYPE_rtp_moonlight_pay_video, nullptr);
  g_object_set(no_key, "encrypt", TRUE, nullptr);
  auto no_key_buf = make_buf();
  REQUIRE(gst_moonlight_video::encrypt_rtp_packets(*no_key, gst_moonlight_video::split_into_rtp(no_key, no_key_buf)) ==
          nullptr);

  gst_buffer_list_unref(plain);
  gst_buffer_list_unref(encrypted);
  gst_buffer_unref(plain_buf);
  gst_buffer_unref(enc_buf);
  gst_buffer_unref(no_key_buf);
  g_object_unref(plain_pay);
  g_object_unref(enc_pay);
  g_object_unref(no_key);
}

TEST_CASE("Control stream encryption V2", "[Encryption]") {
  using namespace moonlight::control;
  crypto::aes_gcm gcm(KEY);
  auto payload = crypto::hex_to_str("060212000000000E05000000033400C00000059F0329"); // an INPUT_DATA packet

  SECTION("Client packets decrypt with the full 32-bit sequence number") {
    // encryptControlMessage() with SS_ENC_CONTROL_V2: 12-byte IV, 'C' 'C'
    std::uint32_t seq = 0x01020304;
    auto [ciphertext, tag] = crypto::aes_encrypt_gcm(payload, KEY, client_iv(seq, 'C', 'C'), 12);
    std::uint16_t length = 4 + 16 + ciphertext.size();
    auto wire = le32(0x0001 | (std::uint32_t(length) << 16)) + le32(seq) + tag + ciphertext;

    REQUIRE(valid_encrypted_packet((std::uint8_t *)wire.data(), wire.size()));
    auto packet = (ControlEncryptedPacket *)wire.data();
    auto opened = decrypt_packet_v2(*packet, gcm);
    REQUIRE(opened);
    REQUIRE(*opened == payload);

    // V1 (8-bit IV) can't read it, and a flipped bit fails authentication
    REQUIRE_THROWS(decrypt_packet(*packet, "9d804e47a6aa6624b7d4b502b32cc522"));
    wire[wire.size() - 1] ^= 1;
    REQUIRE_FALSE(decrypt_packet_v2(*(ControlEncryptedPacket *)wire.data(), gcm));
  }

  SECTION("Host packets decrypt in the client") {
    std::uint32_t seq = 0xABCDEF01;
    auto packet = encrypt_packet_v2(gcm, seq, payload);
    REQUIRE(boost::endian::little_to_native(packet->seq) == seq);
    auto ciphertext = std::string_view(packet->payload, payload.size());
    auto decrypted =
        crypto::aes_decrypt_gcm(ciphertext, KEY, {packet->gcm_tag, GCM_TAG_SIZE}, client_iv(seq, 'H', 'C'), 12);
    REQUIRE(decrypted == payload);
    REQUIRE_THROWS(encrypt_packet_v2(gcm, 1, std::string(MAX_PAYLOAD_SIZE + 1, 'x')));
  }

  SECTION("Malformed framing is rejected before decryption") {
    std::string runt = crypto::hex_to_str("0100");
    REQUIRE_FALSE(valid_encrypted_packet((std::uint8_t *)runt.data(), runt.size()));
    // Declares 200 bytes but carries far fewer
    auto lying = le32(0x0001 | (200u << 16)) + le32(1) + std::string(16, 't') + "x";
    REQUIRE_FALSE(valid_encrypted_packet((std::uint8_t *)lying.data(), lying.size()));
    // Not an ENCRYPTED packet
    auto other = le32(0x0302 | (21u << 16)) + le32(1) + std::string(16, 't') + "x";
    REQUIRE_FALSE(valid_encrypted_packet((std::uint8_t *)other.data(), other.size()));
  }
}

TEST_CASE("Encryption policy", "[Encryption]") {
  REQUIRE(encryption::parse_mode("required", encryption::Mode::DISABLED) == encryption::Mode::REQUIRED);
  REQUIRE(encryption::parse_mode("2", encryption::Mode::DISABLED) == encryption::Mode::REQUIRED);
  REQUIRE(encryption::parse_mode("Optional", encryption::Mode::DISABLED) == encryption::Mode::OPTIONAL);
  REQUIRE(encryption::parse_mode("0", encryption::Mode::REQUIRED) == encryption::Mode::DISABLED);
  REQUIRE(encryption::parse_mode("bogus", encryption::Mode::OPTIONAL) == encryption::Mode::OPTIONAL);

  for (auto ip : {"127.0.0.1",
                  "10.1.2.3",
                  "172.16.0.9",
                  "172.31.255.1",
                  "192.168.0.12",
                  "100.64.1.2",
                  "169.254.1.1",
                  "::1",
                  "fe80::1",
                  "fd12::1",
                  "::ffff:192.168.1.5"}) {
    INFO(ip);
    REQUIRE(state::is_lan_address(ip));
  }
  for (auto ip : {"8.8.8.8", "172.32.0.1", "100.128.0.1", "2001:db8::1", "::ffff:1.1.1.1", "not an ip"}) {
    INFO(ip);
    REQUIRE_FALSE(state::is_lan_address(ip));
  }

  using events::StreamEncryption;
  StreamEncryption old_client(KEY, encryption::Mode::REQUIRED, -1);
  REQUIRE(old_client.supported() == 0);
  REQUIRE_FALSE(old_client.rtsp);

  StreamEncryption lan(KEY, encryption::Mode::DISABLED, 1);
  REQUIRE(lan.supported() == (encryption::CONTROL_V2 | encryption::AUDIO));
  REQUIRE(lan.requested() == 0);
  REQUIRE_FALSE(lan.rtsp);

  StreamEncryption optional(KEY, encryption::Mode::OPTIONAL, 0);
  REQUIRE(optional.supported() == encryption::ALL);
  REQUIRE(optional.requested() == 0);
  REQUIRE_FALSE(optional.rtsp); // corever 0 has no encrypted RTSP

  StreamEncryption required(KEY, encryption::Mode::REQUIRED, 1);
  REQUIRE(required.requested() == encryption::ALL);
  REQUIRE(required.rtsp);
}

TEST_CASE("Launch advertises encrypted RTSP", "[Encryption]") {
  auto url = [](const XML &xml) { return xml.get<std::string>("root.sessionUrl0"); };
  REQUIRE_THAT(url(moonlight::launch_success("1.2.3.4", "48010", true)), Equals("rtspenc://1.2.3.4:48010"));
  REQUIRE_THAT(url(moonlight::launch_success("1.2.3.4", "48010")), Equals("rtsp://1.2.3.4:48010"));
  REQUIRE_THAT(url(moonlight::launch_resume("1.2.3.4", "48010", true)), Equals("rtspenc://1.2.3.4:48010"));
  auto error = moonlight::launch_error(403, "Encryption is required");
  REQUIRE(error.get<int>("root.<xmlattr>.status_code") == 403);
  REQUIRE_THAT(error.get<std::string>("root.<xmlattr>.status_message"), Equals("Encryption is required"));
}
