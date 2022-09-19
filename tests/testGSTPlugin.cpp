extern "C" {
#include <moonlight-common-c/reedsolomon/rs.h>
}

#include <streaming/gst-plugin/audio.hpp>
#include <streaming/gst-plugin/video.hpp>

/* UTILS */

static state::VideoRTPHeaders *get_rtp_video_from_buf(GstBuffer *buf) {
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  return (state::VideoRTPHeaders *)info.data;
}

static state::AudioRTPHeaders *get_rtp_audio_from_buf(GstBuffer *buf) {
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  return (state::AudioRTPHeaders *)info.data;
}

static char *get_c_str_from_buf(GstBuffer *buf) {
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);
  return (char *)info.data;
}

class GStreamerTestsFixture {
public:
  GStreamerTestsFixture() {
    gst_init(nullptr, nullptr);
    reed_solomon_init();
  }
};

/* TESTS */

TEST_CASE_METHOD(GStreamerTestsFixture, "Basic utils", "[GSTPlugin]") {
  auto buffer = gst_buffer_new_and_fill(10, 0);
  REQUIRE_THAT(gst_buffer_copy_content(buffer),
               Catch::Matchers::SizeIs(10) && Equals(std::vector<unsigned char>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  gst_buffer_unref(buffer);

  auto payload = "char array"s;
  buffer = gst_buffer_new_and_fill(10, payload.c_str());
  REQUIRE_THAT(
      gst_buffer_copy_content(buffer),
      Catch::Matchers::SizeIs(payload.size()) && Equals(std::vector<unsigned char>(payload.begin(), payload.end())));
  gst_buffer_unref(buffer);
}

TEST_CASE_METHOD(GStreamerTestsFixture, "Create RTP VIDEO packets", "[GSTPlugin]") {
  auto rtpmoonlightpay = (gst_rtp_moonlight_pay_video *)g_object_new(gst_TYPE_rtp_moonlight_pay_video, nullptr);

  rtpmoonlightpay->payload_size = 10; // This will include 8bytes of payload header (017charss)
  rtpmoonlightpay->fec_percentage = 50;

  auto payload = gst_buffer_new_and_fill(10, "$A PAYLOAD");
  auto video_payload = gst_moonlight_video::prepend_video_header(payload);
  auto rtp_packets = gst_moonlight_video::generate_rtp_packets(*rtpmoonlightpay, video_payload);

  // 10 bytes of actual payload + 8 bytes of payload header
  // will be splitted in two RTP packets
  REQUIRE(gst_buffer_list_length(rtp_packets) == 2);

  SECTION("First packet") {
    auto buf = gst_buffer_list_get(rtp_packets, 0);
    auto rtp_packet = get_rtp_video_from_buf(buf);

    REQUIRE(rtp_packet->packet.flags == FLAG_CONTAINS_PIC_DATA + FLAG_SOF);
    REQUIRE(rtp_packet->packet.frameIndex == 0);
    REQUIRE(rtp_packet->packet.streamPacketIndex == 0);
    REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)0));

    auto rtp_payload = gst_buffer_copy_content(buf, sizeof(state::VideoRTPHeaders));
    auto expected_result = "\0017charss$A"s;
    REQUIRE_THAT(rtp_payload,
                 Catch::Matchers::SizeIs(10) &&
                     Equals(std::vector<unsigned char>(expected_result.begin(), expected_result.end())));
  }

  SECTION("Second packet") {
    auto buf = gst_buffer_list_get(rtp_packets, 1);
    auto rtp_packet = get_rtp_video_from_buf(buf);

    REQUIRE(rtp_packet->packet.flags == FLAG_CONTAINS_PIC_DATA + FLAG_EOF);
    REQUIRE(rtp_packet->packet.frameIndex == 0);
    REQUIRE(rtp_packet->packet.streamPacketIndex == 0x100);
    REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)1));

    auto rtp_payload = gst_buffer_copy_content(buf, sizeof(state::VideoRTPHeaders));
    auto expected_result = " PAYLOAD\0\0"s; // Will contain extra 0x0 padding at the end
    REQUIRE_THAT(rtp_payload,
                 Catch::Matchers::SizeIs(10) &&
                     Equals(std::vector<unsigned char>(expected_result.begin(), expected_result.end())));
  }

  SECTION("FEC") {
    auto rtp_fec_packets = gst_moonlight_video::generate_fec_packets(*rtpmoonlightpay, rtp_packets, payload);
    // Will append min_required_fec_packets to the original payload packets
    REQUIRE(gst_buffer_list_length(rtp_fec_packets) == 4);

    SECTION("First packet (payload)") {
      auto buf = gst_buffer_list_get(rtp_fec_packets, 0);
      auto original = gst_buffer_list_get(rtp_packets, 0);

      // Is the RTP Header left untouched?
      REQUIRE_THAT(gst_buffer_copy_content(original, 0, sizeof(RTP_PACKET)),
                   Equals(gst_buffer_copy_content(buf, 0, sizeof(RTP_PACKET))));

      // Is the payload left untouched?
      REQUIRE_THAT(gst_buffer_copy_content(original, sizeof(state::VideoRTPHeaders)),
                   Equals(gst_buffer_copy_content(buf, sizeof(state::VideoRTPHeaders))));
    }

    SECTION("Second packet (payload)") {
      auto buf = gst_buffer_list_get(rtp_fec_packets, 1);
      auto original = gst_buffer_list_get(rtp_packets, 1);

      // Is the RTP Header left untouched?
      REQUIRE_THAT(gst_buffer_copy_content(original, 0, sizeof(RTP_PACKET)),
                   Equals(gst_buffer_copy_content(buf, 0, sizeof(RTP_PACKET))));

      // Is the payload left untouched?
      REQUIRE_THAT(gst_buffer_copy_content(original, sizeof(state::VideoRTPHeaders)),
                   Equals(gst_buffer_copy_content(buf, sizeof(state::VideoRTPHeaders))));
    }

    SECTION("Third packet (FEC)") {
      auto buf = gst_buffer_list_get(rtp_fec_packets, 2);
      auto rtp_packet = get_rtp_video_from_buf(buf);

      REQUIRE(rtp_packet->packet.frameIndex == 0);
      REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)2));
    }

    SECTION("REED SOLOMON") {
      auto data_shards = 2;
      auto parity_shards = 2;
      auto packet_size = rtpmoonlightpay->payload_size + (int)sizeof(state::VideoRTPHeaders);
      auto total_shards = data_shards + parity_shards;

      auto flatten_packets = gst_buffer_list_unfold(rtp_fec_packets);
      auto packets_content = gst_buffer_copy_content(flatten_packets);

      unsigned char *packets_ptr[total_shards];
      for (int shard_idx = 0; shard_idx < total_shards; shard_idx++) {
        packets_ptr[shard_idx] = &packets_content.front() + (shard_idx * packet_size);
      }

      SECTION("If no package is marked nothing should change") {
        std::vector<unsigned char> marks = {0, 0, 0, 0};

        auto rs = reed_solomon_new(data_shards, parity_shards);
        auto result = reed_solomon_reconstruct(rs, packets_ptr, &marks.front(), total_shards, packets_content.size());

        REQUIRE(result == 0);
        REQUIRE_THAT(packets_content, Equals(gst_buffer_copy_content(flatten_packets)));
      }

      SECTION("Missing one packet should still lead to successfully reconstruct") {
        auto missing_pkt = std::vector<unsigned char>(packet_size);
        packets_ptr[0] = &missing_pkt[0];
        std::vector<unsigned char> marks = {1, 0, 0, 0};

        auto rs = reed_solomon_new(data_shards, parity_shards);
        auto result = reed_solomon_reconstruct(rs, packets_ptr, &marks.front(), total_shards, packet_size);

        REQUIRE(result == 0);

        // Here the packet headers will be wrongly reconstructed because we are manually
        // modifying the parity packets after creation
        // We can only check the packet payload here which should be correctly reconstructed
        auto missing_pkt_payload = std::vector<unsigned char>(
            missing_pkt.begin() + sizeof(state::VideoRTPHeaders),
            missing_pkt.begin() + sizeof(state::VideoRTPHeaders) + rtpmoonlightpay->payload_size);
        auto first_packet_pay_before_fec = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 0),
                                                                   sizeof(state::VideoRTPHeaders),
                                                                   rtpmoonlightpay->payload_size);
        REQUIRE_THAT(missing_pkt_payload, Equals(first_packet_pay_before_fec));
      }
    }
  }
}

TEST_CASE_METHOD(GStreamerTestsFixture, "Encrypt GstBuffer", "[GSTPlugin]") {
  auto payload = gst_buffer_new_and_fill(10, "$A PAYLOAD");
  auto aes_key = "0123456789012345"s;
  auto aes_iv = "12345678"s;
  auto cur_seq_number = 0;

  auto iv_str = derive_iv(aes_iv, cur_seq_number);
  auto encrypted = encrypt_payload(aes_key, iv_str, payload);
  std::string encrypted_str(get_c_str_from_buf(encrypted), gst_buffer_get_size(encrypted));

  auto decrypted = crypto::aes_decrypt_cbc(encrypted_str, aes_key, iv_str, true);
  REQUIRE_THAT(gst_buffer_copy_content(payload),
               Equals(std::vector<unsigned char>(decrypted.begin(), decrypted.end())));
}

TEST_CASE_METHOD(GStreamerTestsFixture, "Audio RTP packet creation", "[GSTPlugin]") {
  auto rtpmoonlightpay = (gst_rtp_moonlight_pay_audio *)g_object_new(gst_TYPE_rtp_moonlight_pay_audio, nullptr);

  rtpmoonlightpay->encrypt = true;
  rtpmoonlightpay->aes_key = "0123456789012345";
  rtpmoonlightpay->aes_iv = "12345678";

  auto payload_str = "TUNZ TUNZ TUMP TUMP!"s;
  auto payload = gst_buffer_new_and_fill(payload_str.size(), payload_str.c_str());
  auto rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);

  REQUIRE(gst_buffer_list_length(rtp_packets) == 1);
  REQUIRE(rtpmoonlightpay->cur_seq_number == 1);

  SECTION("First packet") {
    auto buf = gst_buffer_list_get(rtp_packets, 0);
    auto rtp_packet = get_rtp_audio_from_buf(buf);

    REQUIRE(rtp_packet->rtp.ssrc == 0);
    REQUIRE(rtp_packet->rtp.packetType == 97);
    REQUIRE(rtp_packet->rtp.header == 0x80);
    REQUIRE(rtp_packet->rtp.sequenceNumber == 0);
    REQUIRE(rtp_packet->rtp.timestamp == 0);

    auto rtp_payload = gst_buffer_copy_content(buf, sizeof(state::AudioRTPHeaders));

    auto decrypted = crypto::aes_decrypt_cbc(std::string(rtp_payload.begin(), rtp_payload.end()),
                                             rtpmoonlightpay->aes_key,
                                             derive_iv(rtpmoonlightpay->aes_iv, rtpmoonlightpay->cur_seq_number - 1),
                                             true);
    REQUIRE_THAT(decrypted, Equals(payload_str));
  }

  rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);
  REQUIRE(gst_buffer_list_length(rtp_packets) == 1);
  REQUIRE(rtpmoonlightpay->cur_seq_number == 2);

  SECTION("Second packet") {
    auto buf = gst_buffer_list_get(rtp_packets, 0);
    auto rtp_packet = get_rtp_audio_from_buf(buf);

    REQUIRE(rtp_packet->rtp.ssrc == 0);
    REQUIRE(rtp_packet->rtp.packetType == 97);
    REQUIRE(rtp_packet->rtp.header == 0x80);
    REQUIRE(boost::endian::big_to_native(rtp_packet->rtp.sequenceNumber) == 1);
    REQUIRE(boost::endian::big_to_native(rtp_packet->rtp.timestamp) == 5);

    auto rtp_payload = gst_buffer_copy_content(buf, sizeof(state::AudioRTPHeaders));

    auto decrypted = crypto::aes_decrypt_cbc(std::string(rtp_payload.begin(), rtp_payload.end()),
                                             rtpmoonlightpay->aes_key,
                                             derive_iv(rtpmoonlightpay->aes_iv, rtpmoonlightpay->cur_seq_number - 1),
                                             true);
    REQUIRE_THAT(decrypted, Equals(payload_str));
  }

  rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);
  REQUIRE(gst_buffer_list_length(rtp_packets) == 1);
  REQUIRE(rtpmoonlightpay->cur_seq_number == 3);

  rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);
  REQUIRE(gst_buffer_list_length(rtp_packets) == 3);
  REQUIRE(rtpmoonlightpay->cur_seq_number == 4);
}
