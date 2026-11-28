#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

#include <cstring>
#include <moonlight/control.hpp>
using namespace moonlight::control;

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

    encrypted_packet.gcm_tag[0] ^= 1;
    REQUIRE_THROWS_AS(decrypt_packet(encrypted_packet, aes_key), std::runtime_error);
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

TEST_CASE("Control decrypt reads only the received packet", "CONTROL") {
  std::string aes_key = "EDF04A215C4FBEA20934120C8480D855";
  std::string payload = crypto::hex_to_str("020302000000");

  SECTION("Any GCM tag, with or without NUL bytes, decrypts") {
    bool tested_nul_tag = false;
    for (std::uint32_t seq = 0; seq < 256; seq++) {
      auto encrypted = encrypt_packet(aes_key, seq, payload);
      tested_nul_tag |= std::string_view{encrypted->gcm_tag, GCM_TAG_SIZE}.find('\0') != std::string_view::npos;

      // ENet hands us a heap buffer of exactly the received size: under ASan any read past it aborts
      auto size = encrypted->full_size();
      auto received = std::make_unique<char[]>(size);
      std::memcpy(received.get(), encrypted.get(), size);
      auto &packet = *(ControlEncryptedPacket *)received.get();

      REQUIRE(packet.fits_in(size));
      REQUIRE_THAT(decrypt_packet(packet, aes_key), Equals(payload));
    }
    REQUIRE(tested_nul_tag);
  }

  SECTION("Packets shorter than their header claims are rejected") {
    auto encrypted = encrypt_packet(aes_key, 0, payload);
    auto size = encrypted->full_size();
    REQUIRE(encrypted->fits_in(size));
    REQUIRE_FALSE(encrypted->fits_in(size - 1));
    REQUIRE_FALSE(encrypted->fits_in(sizeof(ControlPacket)));

    encrypted->header.length = boost::endian::native_to_little<std::uint16_t>(sizeof(std::uint32_t) + GCM_TAG_SIZE - 1);
    REQUIRE_FALSE(encrypted->fits_in(size));
  }

  SECTION("Payloads too short for their own ControlPacket header are rejected") {
    // They'd decrypt to fewer bytes than the ControlPacket type that is read from them
    for (auto short_payload : {std::string_view{}, std::string_view{"\x01", 1}, std::string_view{"\x01\x02\x03", 3}}) {
      auto encrypted = encrypt_packet(aes_key, 0, short_payload);
      REQUIRE_FALSE(encrypted->fits_in(encrypted->full_size()));
    }
    auto smallest = encrypt_packet(aes_key, 0, std::string_view{"\x01\x02\x03\x04", 4});
    REQUIRE(smallest->fits_in(smallest->full_size()));
  }
}

TEST_CASE("Control V2 uses distinct wire nonces for each direction", "CONTROL") {
  const std::string key = "EDF04A215C4FBEA20934120C8480D855";
  const auto payload = crypto::hex_to_str("020302000000");
  const auto mode = ControlMode::v2;
  const std::uint32_t sequence = 0x01020304;

  // Full packets independently generated with CryptoKit.
  const auto client = encrypt_packet(key, sequence, payload, mode, ControlOrigin::client);
  REQUIRE_THAT(crypto::str_to_hex(to_string(*client)),
               Equals("01001A0004030201E024199442ADB229428DCE814B8D3CFC8850D54BBA1B"));
  REQUIRE(decrypt_packet(*client, key, mode) == payload);

  const auto host = encrypt_packet(key, sequence, payload, mode);
  REQUIRE_THAT(crypto::str_to_hex(to_string(*host)),
               Equals("01001A0004030201AAB21D05090B9AE217884AF63B145E3EAE4C7DDC2E65"));
  REQUIRE(decrypt_packet(*host, key, mode, ControlOrigin::host) == payload);
  REQUIRE_THROWS_AS(decrypt_packet(*host, key, mode), std::runtime_error);
}

TEST_CASE("Control mode and sequence are shared for one key", "CONTROL") {
  ControlEncryption state;
  REQUIRE_FALSE(state.next_host_sequence());
  REQUIRE_FALSE(state.claim_peer());

  SECTION("Clients can select Control V2") {
    REQUIRE(state.negotiate(CONTROL_V2_FLAG));
    REQUIRE(state.mode() == ControlMode::v2);
    REQUIRE(state.claim_peer());
    REQUIRE_FALSE(state.claim_peer());
    for (std::uint32_t sequence = 0; sequence < 258; ++sequence) {
      REQUIRE(state.next_host_sequence() == sequence);
    }
    REQUIRE_FALSE(state.negotiate(0));
    REQUIRE(state.negotiate(CONTROL_V2_FLAG));
    REQUIRE(state.next_host_sequence() == 258);
  }

  SECTION("Clients that omit the flag retain legacy control") {
    REQUIRE(state.negotiate(0));
    REQUIRE(state.mode() == ControlMode::legacy);
    REQUIRE(state.claim_peer());
    REQUIRE(state.claim_peer());
    REQUIRE(state.next_host_sequence() == 0);
    REQUIRE(state.next_host_sequence() == 0);
    REQUIRE_FALSE(state.negotiate(CONTROL_V2_FLAG));
  }
}

TEST_CASE("Reused legacy keys cannot negotiate Control V2", "CONTROL") {
  ControlEncryption state(false);
  REQUIRE_FALSE(state.negotiate(CONTROL_V2_FLAG));
  REQUIRE(state.mode() == ControlMode::pending);
  REQUIRE(state.negotiate(0));
  REQUIRE(state.claim_peer());
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