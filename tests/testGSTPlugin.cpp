#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_contains.hpp>

using Catch::Matchers::Equals;

#include <gst-plugin/audio.hpp>
#include <gst-plugin/video.hpp>
#include <moonlight/fec.hpp>
#include <string>

using namespace std::string_literals;

/* UTILS */

static std::pair<char * /* data */, unsigned long /* size */> copy_buffer_data(GstBuffer *buf) {
  auto size = gst_buffer_get_size(buf);
  auto *res = new char[size];

  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  std::copy(info.data, info.data + size, res);
  gst_buffer_unmap(buf, &info);

  return {res, size};
}

static audio::AudioRTPHeaders *get_rtp_audio_from_buf(GstBuffer *buf) {
  return (audio::AudioRTPHeaders *)copy_buffer_data(buf).first;
}

static std::string get_str_from_buf(GstBuffer *buf) {
  auto copy = copy_buffer_data(buf);
  return {copy.first, copy.second};
}

static int get_buf_refcount(GstBuffer *buf) {
  return buf->mini_object.refcount;
}

class GStreamerTestsFixture {
public:
  GStreamerTestsFixture() {
    gst_init(nullptr, nullptr);
    moonlight::fec::init();
  }
};

/*
 * BASE UTILS
 */

TEST_CASE_METHOD(GStreamerTestsFixture, "Basic utils", "[GSTPlugin]") {
  auto buffer = gst_buffer_new_and_fill(10, 0);
  REQUIRE_THAT(gst_buffer_copy_content(buffer),
               Catch::Matchers::SizeIs(10) && Equals(std::vector<unsigned char>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  /* Cleanup */
  REQUIRE(get_buf_refcount(buffer) == 1);
  gst_buffer_unref(buffer);

  auto payload = "char array"s;
  buffer = gst_buffer_new_and_fill(10, payload.c_str());
  REQUIRE_THAT(
      gst_buffer_copy_content(buffer),
      Catch::Matchers::SizeIs(payload.size()) && Equals(std::vector<unsigned char>(payload.begin(), payload.end())));
  REQUIRE_THAT(get_str_from_buf(buffer), Catch::Matchers::SizeIs(payload.size()) && Equals(payload));

  /* Cleanup */
  REQUIRE(get_buf_refcount(buffer) == 1);
  gst_buffer_unref(buffer);
}

TEST_CASE_METHOD(GStreamerTestsFixture, "Encrypt GstBuffer", "[GSTPlugin]") {
  auto payload = gst_buffer_new_and_fill(10, "$A PAYLOAD");
  auto aes_key = "0123456789012345"s;
  auto aes_iv = "12345678"s;
  auto cur_seq_number = 0;

  auto iv_str = derive_iv(aes_iv, cur_seq_number);

  REQUIRE_THAT(iv_str, Equals("\000\274aN\000\000\000\000\000\000\000\000\000\000\000\000"s));

  auto encrypted = encrypt_payload(aes_key, iv_str, payload);
  auto encrypted_str = get_str_from_buf(encrypted);

  auto decrypted = crypto::aes_decrypt_cbc(encrypted_str, aes_key, iv_str, true);
  REQUIRE_THAT(gst_buffer_copy_content(payload),
               Equals(std::vector<unsigned char>(decrypted.begin(), decrypted.end())));

  /* Cleanup */
  REQUIRE(get_buf_refcount(payload) == 1);
  gst_buffer_unref(payload);
}

/*
 * VIDEO
 */
TEST_CASE_METHOD(GStreamerTestsFixture, "RTP VIDEO Splits", "[GSTPlugin]") {
  auto rtpmoonlightpay = (gst_rtp_moonlight_pay_video *)g_object_new(gst_TYPE_rtp_moonlight_pay_video, nullptr);

  auto payload_str = "Never gonna give you up\n"
                     "Never gonna let you down\n"
                     "Never gonna run around and desert you\n"
                     "Never gonna make you cry\n"
                     "Never gonna say goodbye\n"
                     "Never gonna tell a lie and hurt you"s;

  auto rtp_header_size = (int)sizeof(gst_moonlight_video::VideoRTPHeaders);
  auto rtp_payload_header_size = 8; // 017charss
  rtpmoonlightpay->payload_size = 32;
  rtpmoonlightpay->fec_percentage = 50;
  rtpmoonlightpay->add_padding = false;

  auto payload_buf = gst_buffer_new_and_fill(payload_str.size(), payload_str.c_str());
  auto rtp_packets = gst_moonlight_video::split_into_rtp(rtpmoonlightpay, payload_buf);

  auto payload_expected_packets = std::ceil((float)(payload_str.size() + rtp_payload_header_size) /
                                            ((float)rtpmoonlightpay->payload_size - MAX_RTP_HEADER_SIZE));
  auto fec_expected_packets = std::ceil(payload_expected_packets * ((double)rtpmoonlightpay->fec_percentage / 100));

  REQUIRE(gst_buffer_list_length(rtp_packets) == payload_expected_packets + fec_expected_packets);

  std::string returned_payload = ""s;
  for (auto i = 0; i < payload_expected_packets; i++) {
    auto buf = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, i), rtp_header_size);
    returned_payload +=
        std::string(buf.begin() + (long)(i == 0 ? sizeof(gst_moonlight_video::VideoShortHeader) : 0), buf.end());
  }
  REQUIRE_THAT(returned_payload, Equals(payload_str));

  SECTION("Multi block FEC") {
    auto payload_buf_blocks = gst_buffer_new_and_fill(payload_str.size(), payload_str.c_str());
    auto rtp_packets_blocks = gst_moonlight_video::generate_rtp_packets(*rtpmoonlightpay, payload_buf_blocks);
    auto final_packets = gst_moonlight_video::generate_fec_multi_blocks(rtpmoonlightpay,
                                                                        rtp_packets_blocks,
                                                                        (int)payload_expected_packets,
                                                                        payload_buf_blocks);

    REQUIRE(gst_buffer_list_length(final_packets) ==
            payload_expected_packets + fec_expected_packets - 1); // TODO: why one less?

    auto first_payload = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 0), rtp_header_size);
    REQUIRE_THAT(
        std::string(first_payload.begin() + sizeof(gst_moonlight_video::VideoShortHeader), first_payload.end()),
        Equals("Never go"));
    // TODO: proper check content and FEC
  }

  /* Cleanup */
  REQUIRE(GST_OBJECT_REFCOUNT(rtpmoonlightpay) == 1);
  g_object_unref(rtpmoonlightpay);
  REQUIRE(get_buf_refcount(payload_buf) == 1);
  g_object_unref(payload_buf);
}

TEST_CASE_METHOD(GStreamerTestsFixture, "Create RTP VIDEO packets", "[GSTPlugin]") {
  auto rtpmoonlightpay = (gst_rtp_moonlight_pay_video *)g_object_new(gst_TYPE_rtp_moonlight_pay_video, nullptr);

  rtpmoonlightpay->payload_size = 10 + MAX_RTP_HEADER_SIZE; // This will include 8bytes of payload header (017charss)
  rtpmoonlightpay->fec_percentage = 50;
  rtpmoonlightpay->add_padding = true;
  auto rtp_packet_size = rtpmoonlightpay->payload_size + sizeof(moonlight::NV_VIDEO_PACKET);
  auto rtp_header_size = (long)sizeof(gst_moonlight_video::VideoRTPHeaders);

  auto payload = gst_buffer_new_and_fill(10, "$A PAYLOAD");
  auto video_payload = gst_moonlight_video::prepend_video_header(*rtpmoonlightpay, payload);
  auto rtp_packets = gst_moonlight_video::generate_rtp_packets(*rtpmoonlightpay, video_payload);

  // 10 bytes of actual payload + 8 bytes of payload header
  // will be splitted in two RTP packets
  REQUIRE(gst_buffer_get_size(video_payload) == gst_buffer_get_size(payload) + 8); // Added 017charss
  REQUIRE(gst_buffer_list_length(rtp_packets) == 2);

  SECTION("First packet") {
    auto first_packet = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 0));
    auto rtp_packet = reinterpret_cast<gst_moonlight_video::VideoRTPHeaders *>(first_packet.data());

    REQUIRE(rtp_packet->packet.flags == FLAG_CONTAINS_PIC_DATA + FLAG_SOF);
    REQUIRE(rtp_packet->packet.frameIndex == 0);
    REQUIRE(rtp_packet->packet.streamPacketIndex == 0);
    REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)0));

    auto short_header =
        reinterpret_cast<gst_moonlight_video::VideoShortHeader *>(first_packet.data() + rtp_header_size);
    REQUIRE(short_header->frame_type == 2);
    REQUIRE(short_header->header_type == 1);
    REQUIRE(short_header->last_payload_len == 8);

    auto rtp_payload = std::string(
        first_packet.begin() + rtp_header_size + sizeof(gst_moonlight_video::VideoShortHeader),
        first_packet.end());
    REQUIRE_THAT("$A"s, Equals(rtp_payload));
  }

  SECTION("Second packet") {
    auto second_packet = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 1));
    auto rtp_packet = reinterpret_cast<gst_moonlight_video::VideoRTPHeaders *>(second_packet.data());

    REQUIRE(rtp_packet->packet.flags == FLAG_CONTAINS_PIC_DATA + FLAG_EOF);
    REQUIRE(rtp_packet->packet.frameIndex == 0);
    REQUIRE(rtp_packet->packet.streamPacketIndex == 0x100);
    REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)1));

    auto rtp_payload = std::string(second_packet.begin() + rtp_header_size, second_packet.end());
    REQUIRE_THAT(" PAYLOAD\0\0"s, Equals(rtp_payload));
  }

  SECTION("FEC") {
    gst_moonlight_video::generate_fec_packets(*rtpmoonlightpay, rtp_packets, payload);
    // Will append min_required_fec_packets to the original payload packets
    REQUIRE(gst_buffer_list_length(rtp_packets) == 4);

    SECTION("First packet (payload)") {
      auto first_packet = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 0));

      auto rtp_packet = reinterpret_cast<gst_moonlight_video::VideoRTPHeaders *>(first_packet.data());

      REQUIRE(rtp_packet->packet.flags == FLAG_CONTAINS_PIC_DATA + FLAG_SOF);
      REQUIRE(rtp_packet->packet.frameIndex == 0);
      REQUIRE(rtp_packet->packet.streamPacketIndex == 0);
      REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)0));

      // FEC additional info
      REQUIRE(rtp_packet->packet.fecInfo == 8390208);
      REQUIRE(rtp_packet->packet.multiFecBlocks == 0);
      REQUIRE(rtp_packet->packet.multiFecFlags == 0x10);

      auto short_header =
          reinterpret_cast<gst_moonlight_video::VideoShortHeader *>(first_packet.data() + rtp_header_size);
      REQUIRE(short_header->frame_type == 2);
      REQUIRE(short_header->header_type == 1);
      REQUIRE(short_header->last_payload_len == 8);

      auto rtp_payload = std::string(
          first_packet.begin() + rtp_header_size + sizeof(gst_moonlight_video::VideoShortHeader),
          first_packet.end());
      REQUIRE_THAT("$A"s, Equals(rtp_payload));
    }

    SECTION("Second packet (payload)") {
      auto second_packet = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 1));
      auto rtp_packet = reinterpret_cast<gst_moonlight_video::VideoRTPHeaders *>(second_packet.data());

      REQUIRE(rtp_packet->packet.flags == FLAG_CONTAINS_PIC_DATA + FLAG_EOF);
      REQUIRE(rtp_packet->packet.frameIndex == 0);
      REQUIRE(rtp_packet->packet.streamPacketIndex == 0x100);
      REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)1));

      // FEC additional info
      REQUIRE(rtp_packet->packet.fecInfo == 8394304);
      REQUIRE(rtp_packet->packet.multiFecBlocks == 0);
      REQUIRE(rtp_packet->packet.multiFecFlags == 0x10);

      auto rtp_payload = std::string(second_packet.begin() + rtp_header_size, second_packet.end());
      REQUIRE_THAT(" PAYLOAD\0\0"s, Equals(rtp_payload));
    }

    SECTION("Third packet (FEC)") {
      auto third_packet = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 2));
      auto rtp_packet = reinterpret_cast<gst_moonlight_video::VideoRTPHeaders *>(third_packet.data());

      REQUIRE(rtp_packet->packet.frameIndex == 0);
      REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)2));

      // FEC additional info
      REQUIRE(rtp_packet->packet.fecInfo == 8398400);
      REQUIRE(rtp_packet->packet.multiFecBlocks == 0);
      REQUIRE(rtp_packet->packet.multiFecFlags == 0x10);
    }

    SECTION("Fourth packet (FEC)") {
      auto fourth_packet = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 3));
      auto rtp_packet = reinterpret_cast<gst_moonlight_video::VideoRTPHeaders *>(fourth_packet.data());

      REQUIRE(rtp_packet->packet.frameIndex == 0);
      REQUIRE(rtp_packet->rtp.sequenceNumber == boost::endian::native_to_big((uint16_t)3));

      // FEC additional info
      REQUIRE(rtp_packet->packet.fecInfo == 8402496);
      REQUIRE(rtp_packet->packet.multiFecBlocks == 0);
      REQUIRE(rtp_packet->packet.multiFecFlags == 0x10);
    }

    SECTION("REED SOLOMON") {
      auto data_shards = 2;
      auto parity_shards = 2;
      auto total_shards = data_shards + parity_shards;

      auto flatten_packets = gst_buffer_list_unfold(rtp_packets);
      auto packets_content = gst_buffer_copy_content(flatten_packets);

      unsigned char *packets_ptr[total_shards];
      for (int shard_idx = 0; shard_idx < total_shards; shard_idx++) {
        packets_ptr[shard_idx] = &packets_content.front() + (shard_idx * rtp_packet_size);
      }

      SECTION("If no package is marked nothing should change") {
        std::vector<unsigned char> marks = {0, 0, 0, 0};

        auto rs = moonlight::fec::create(data_shards, parity_shards);
        auto result = moonlight::fec::decode(rs.get(), packets_ptr, &marks.front(), total_shards, rtp_packet_size);

        REQUIRE(result == 0);
        REQUIRE_THAT(packets_content, Equals(gst_buffer_copy_content(flatten_packets)));
      }

      SECTION("Missing one packet should still lead to successfully reconstruct") {
        auto missing_pkt = std::vector<unsigned char>(rtp_packet_size);
        packets_ptr[0] = &missing_pkt[0];
        std::vector<unsigned char> marks = {1, 0, 0, 0};

        auto rs = moonlight::fec::create(data_shards, parity_shards);
        auto result = moonlight::fec::decode(rs.get(), packets_ptr, &marks.front(), total_shards, rtp_packet_size);

        REQUIRE(result == 0);

        // Here the packet headers will be wrongly reconstructed because we are manually
        // modifying the parity packets after creation
        // We can only check the packet payload here which should be correctly reconstructed
        auto pay_size = rtpmoonlightpay->payload_size - MAX_RTP_HEADER_SIZE;
        auto missing_pkt_payload = std::vector<unsigned char>(
            missing_pkt.begin() + sizeof(gst_moonlight_video::VideoRTPHeaders),
            missing_pkt.begin() + sizeof(gst_moonlight_video::VideoRTPHeaders) + pay_size);
        auto first_packet_pay_before_fec = gst_buffer_copy_content(gst_buffer_list_get(rtp_packets, 0),
                                                                   sizeof(gst_moonlight_video::VideoRTPHeaders),
                                                                   pay_size);

        REQUIRE_THAT(missing_pkt_payload, Equals(first_packet_pay_before_fec));
      }
    }
  }

  REQUIRE(GST_OBJECT_REFCOUNT(rtpmoonlightpay) == 1);
  g_object_unref(rtpmoonlightpay);
  REQUIRE(get_buf_refcount(payload) == 1);
  g_object_unref(payload);
  g_object_unref(rtp_packets);
  REQUIRE(get_buf_refcount(video_payload) == 1);
  g_object_unref(video_payload);
}

/*
 * AUDIO
 */
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
  auto first_pkt = gst_buffer_list_get(rtp_packets, 0);

  SECTION("First packet") {
    auto rtp_packet = get_rtp_audio_from_buf(first_pkt);

    REQUIRE(rtp_packet->rtp.ssrc == 0);
    REQUIRE(rtp_packet->rtp.packetType == 97);
    REQUIRE(rtp_packet->rtp.header == 0x80);
    REQUIRE(rtp_packet->rtp.sequenceNumber == 0);
    REQUIRE(rtp_packet->rtp.timestamp == 0);

    auto rtp_payload = gst_buffer_copy_content(first_pkt, sizeof(audio::AudioRTPHeaders));

    auto decrypted = crypto::aes_decrypt_cbc(std::string(rtp_payload.begin(), rtp_payload.end()),
                                             rtpmoonlightpay->aes_key,
                                             derive_iv(rtpmoonlightpay->aes_iv, rtpmoonlightpay->cur_seq_number - 1),
                                             true);
    REQUIRE_THAT(decrypted, Equals(payload_str));
  }

  rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);
  REQUIRE(gst_buffer_list_length(rtp_packets) == 1);
  REQUIRE(rtpmoonlightpay->cur_seq_number == 2);
  auto second_pkt = gst_buffer_list_get(rtp_packets, 0);

  SECTION("Second packet") {
    auto rtp_packet = get_rtp_audio_from_buf(second_pkt);

    REQUIRE(rtp_packet->rtp.ssrc == 0);
    REQUIRE(rtp_packet->rtp.packetType == 97);
    REQUIRE(rtp_packet->rtp.header == 0x80);
    REQUIRE(boost::endian::big_to_native(rtp_packet->rtp.sequenceNumber) == 1);
    REQUIRE(boost::endian::big_to_native(rtp_packet->rtp.timestamp) == 5);

    auto rtp_payload = gst_buffer_copy_content(second_pkt, sizeof(audio::AudioRTPHeaders));

    auto decrypted = crypto::aes_decrypt_cbc(std::string(rtp_payload.begin(), rtp_payload.end()),
                                             rtpmoonlightpay->aes_key,
                                             derive_iv(rtpmoonlightpay->aes_iv, rtpmoonlightpay->cur_seq_number - 1),
                                             true);
    REQUIRE_THAT(decrypted, Equals(payload_str));
  }

  rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);
  REQUIRE(gst_buffer_list_length(rtp_packets) == 1);
  REQUIRE(rtpmoonlightpay->cur_seq_number == 3);
  auto third_pkt = gst_buffer_list_get(rtp_packets, 0);

  SECTION("Third packet") {
    auto rtp_packet = get_rtp_audio_from_buf(third_pkt);

    REQUIRE(rtp_packet->rtp.ssrc == 0);
    REQUIRE(rtp_packet->rtp.packetType == 97);
    REQUIRE(rtp_packet->rtp.header == 0x80);
    REQUIRE(boost::endian::big_to_native(rtp_packet->rtp.sequenceNumber) == 2);
    REQUIRE(boost::endian::big_to_native(rtp_packet->rtp.timestamp) == 10);

    auto rtp_payload = gst_buffer_copy_content(third_pkt, sizeof(audio::AudioRTPHeaders));

    auto decrypted = crypto::aes_decrypt_cbc(std::string(rtp_payload.begin(), rtp_payload.end()),
                                             rtpmoonlightpay->aes_key,
                                             derive_iv(rtpmoonlightpay->aes_iv, rtpmoonlightpay->cur_seq_number - 1),
                                             true);
    REQUIRE_THAT(decrypted, Equals(payload_str));
  }

  /* When the 4th packet arrives, we'll also FEC encode all the previous and return
   * the data packet + 2 more FEC packets
   */
  rtp_packets = audio::split_into_rtp(rtpmoonlightpay, payload);
  REQUIRE(gst_buffer_list_length(rtp_packets) == 3); // One data packet + 2 FEC packets
  REQUIRE(rtpmoonlightpay->cur_seq_number == 4);

  SECTION("FEC") {
    SECTION("First FEC packet") {
      auto fec_packet = (audio::AudioFECPacket *)copy_buffer_data((gst_buffer_list_get(rtp_packets, 1))).first;

      REQUIRE(fec_packet->rtp.ssrc == 0);
      REQUIRE(fec_packet->rtp.packetType == 127);
      REQUIRE(fec_packet->rtp.header == 0x80);
      REQUIRE(fec_packet->rtp.timestamp == 0);

      REQUIRE(boost::endian::big_to_native(fec_packet->rtp.sequenceNumber) == 3);
      REQUIRE(fec_packet->fec_header.payloadType == 97);
      REQUIRE(fec_packet->fec_header.ssrc == 0);
      REQUIRE(fec_packet->fec_header.fecShardIndex == 0);
    }

    SECTION("Second FEC packet") {
      auto fec_packet = (audio::AudioFECPacket *)copy_buffer_data((gst_buffer_list_get(rtp_packets, 2))).first;

      REQUIRE(fec_packet->rtp.ssrc == 0);
      REQUIRE(fec_packet->rtp.packetType == 127);
      REQUIRE(fec_packet->rtp.header == 0x80);
      REQUIRE(fec_packet->rtp.timestamp == 0);

      REQUIRE(boost::endian::big_to_native(fec_packet->rtp.sequenceNumber) == 4);
      REQUIRE(fec_packet->fec_header.payloadType == 97);
      REQUIRE(fec_packet->fec_header.ssrc == 0);
      REQUIRE(fec_packet->fec_header.fecShardIndex == 1);
    }
  }

  SECTION("REED SOLOMON") {
    auto packet_size = gst_buffer_get_size(gst_buffer_list_get(rtp_packets, 0));

    SECTION("If no package is marked nothing should change") {
      std::vector<unsigned char> marks = {0, 0, 0, 0, 0, 0};

      auto result = moonlight::fec::decode(rtpmoonlightpay->rs.get(),
                                           rtpmoonlightpay->packets_buffer,
                                           &marks.front(),
                                           AUDIO_TOTAL_SHARDS,
                                           packet_size);

      REQUIRE(result == 0);
    }

    SECTION("Missing one packet should still lead to successful reconstruct") {
      auto original_pkt = gst_buffer_copy_content(first_pkt, sizeof(audio::AudioRTPHeaders));
      auto missing_pkt = std::vector<unsigned char>(packet_size);
      rtpmoonlightpay->packets_buffer[0] = &missing_pkt[0];
      std::vector<unsigned char> marks = {1, 0, 0, 0, 0, 0};

      auto result = moonlight::fec::decode(rtpmoonlightpay->rs.get(),
                                           rtpmoonlightpay->packets_buffer,
                                           &marks.front(),
                                           AUDIO_TOTAL_SHARDS,
                                           packet_size);

      REQUIRE(result == 0);
      REQUIRE_THAT(std::string(missing_pkt.begin() + sizeof(audio::AudioRTPHeaders), missing_pkt.end()),
                   Equals(std::string(original_pkt.begin(), original_pkt.end())));
    }

    g_object_unref(rtpmoonlightpay);
  }
}
