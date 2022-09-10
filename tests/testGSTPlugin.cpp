#include <streaming/streaming.cpp>

/* UTILS */

static std::vector<unsigned char> gst_buffer_copy_content(GstBuffer *buf, unsigned long offset, unsigned long size) {
  auto vals = std::vector<unsigned char>(size);

  /* get READ access to the memory and fill with vals */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  for (int i = 0; i < size; i++) {
    vals[i] = info.data[i + offset];
  }
  gst_buffer_unmap(buf, &info);
  return vals;
}

static std::vector<unsigned char> gst_buffer_copy_content(GstBuffer *buf, unsigned long offset) {
  return gst_buffer_copy_content(buf, offset, gst_buffer_get_size(buf) - offset);
}

static std::vector<unsigned char> gst_buffer_copy_content(GstBuffer *buf) {
  return gst_buffer_copy_content(buf, 0);
}

static state::VideoRTPHeaders *get_rtp_from_buf(GstBuffer *buf) {
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  return (state::VideoRTPHeaders *)info.data;
}

class GStreamerTestsFixture {
public:
  GStreamerTestsFixture() {
    streaming::init();
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

TEST_CASE_METHOD(GStreamerTestsFixture, "Create RTP packets", "[GSTPlugin]") {
  auto rtpmoonlightpay = (gst_rtp_moonlight_pay *)g_object_new(gst_TYPE_rtp_moonlight_pay, nullptr);

  rtpmoonlightpay->payload_size = 10; // This will include 8bytes of payload header (017charss)
  rtpmoonlightpay->fec_percentage = 50;

  auto payload = gst_buffer_new_and_fill(10, "$A PAYLOAD");
  auto video_payload = prepend_video_header(payload);
  auto rtp_packets = generate_rtp_packets(*rtpmoonlightpay, video_payload);

  // 10 bytes of actual payload + 8 bytes of payload header
  // will be splitted in two RTP packets
  REQUIRE(gst_buffer_list_length(rtp_packets) == 2);

  SECTION("First packet") {
    auto buf = gst_buffer_list_get(rtp_packets, 0);
    auto rtp_packet = get_rtp_from_buf(buf);

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
    auto rtp_packet = get_rtp_from_buf(buf);

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
    auto rtp_fec_packets = generate_fec_packets(*rtpmoonlightpay, rtp_packets, payload);
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
      auto rtp_packet = get_rtp_from_buf(buf);

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
