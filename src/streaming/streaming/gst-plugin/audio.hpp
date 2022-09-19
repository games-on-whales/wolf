#pragma once

#include <streaming/gst-plugin/gstrtpmoonlightpay_audio.hpp>
#include <streaming/gst-plugin/utils.hpp>

namespace audio {

/**
 * Creates an RTP header and returns a GstBuffer to it
 */
static GstBuffer *create_rtp_header(const gst_rtp_moonlight_pay_audio &rtpmoonlightpay) {
  constexpr auto rtp_header_size = sizeof(state::AudioRTPHeaders);
  GstBuffer *buf = gst_buffer_new_and_fill(rtp_header_size, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (state::AudioRTPHeaders *)info.data;

  packet->rtp.header = 0x80;
  packet->rtp.packetType = 97;
  packet->rtp.ssrc = 0;

  auto timestamp = rtpmoonlightpay.cur_seq_number * rtpmoonlightpay.packet_duration;
  packet->rtp.sequenceNumber = boost::endian::native_to_big((uint16_t)rtpmoonlightpay.cur_seq_number);
  packet->rtp.timestamp = boost::endian::native_to_big((uint32_t)timestamp);

  gst_buffer_unmap(buf, &info);

  return buf;
}

static GstBuffer *create_rtp_fec_header(const gst_rtp_moonlight_pay_audio &rtpmoonlightpay, int fec_packet_idx) {
  constexpr auto rtp_header_size = sizeof(state::AudioFECPacket);
  GstBuffer *buf = gst_buffer_new_and_fill(rtp_header_size, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (state::AudioFECPacket *)info.data;

  packet->rtp.header = 0x80;
  packet->rtp.packetType = 127;
  packet->rtp.ssrc = 0;
  packet->rtp.timestamp = 0;

  packet->fec_header.payloadType = 97;
  packet->fec_header.ssrc = 0;

  auto base_seq_num = rtpmoonlightpay.cur_seq_number - (AUDIO_DATA_SHARDS - 1);
  auto base_timestamp = base_seq_num * rtpmoonlightpay.packet_duration;
  packet->fec_header.baseSequenceNumber = boost::endian::native_to_big((uint16_t)(base_seq_num));
  packet->fec_header.baseTimestamp = boost::endian::native_to_big((uint32_t)base_timestamp);
  packet->rtp.sequenceNumber =
      boost::endian::native_to_big((uint16_t)(rtpmoonlightpay.cur_seq_number + fec_packet_idx));
  packet->fec_header.fecShardIndex = fec_packet_idx;

  gst_buffer_unmap(buf, &info);

  return buf;
}

GstBuffer *create_rtp_audio_buffer(const gst_rtp_moonlight_pay_audio &rtpmoonlightpay, GstBuffer *inbuf) {
  GstBuffer *payload = inbuf;

  if (rtpmoonlightpay.encrypt) {
    auto derived_iv = derive_iv(rtpmoonlightpay.aes_iv, rtpmoonlightpay.cur_seq_number);
    payload = encrypt_payload(rtpmoonlightpay.aes_key, derived_iv, inbuf);
  }

  auto rtp_header = create_rtp_header(rtpmoonlightpay);
  auto full_rtp_buf = gst_buffer_append(rtp_header, payload);
  gst_copy_timestamps(inbuf, full_rtp_buf);

  return full_rtp_buf;
}

/**
 * Our main function:
 * Given an input buffer containing some kind of payload
 * split it in one or multiple RTP packets following the Moonlight specification.
 *
 * @return a list of buffers, each element representing a single RTP packet
 */
static GstBufferList *split_into_rtp(gst_rtp_moonlight_pay_audio *rtpmoonlightpay, GstBuffer *inbuf) {
  bool time_to_fec = (rtpmoonlightpay->cur_seq_number + 1) % AUDIO_DATA_SHARDS == 0;

  GstBufferList *rtp_packets = gst_buffer_list_new_sized(time_to_fec ? 1 + AUDIO_FEC_SHARDS : 1);

  auto rtp_audio_buf = create_rtp_audio_buffer(*rtpmoonlightpay, inbuf);
  gst_buffer_list_insert(rtp_packets, -1, rtp_audio_buf);

  // save the payload locally
  gst_buffer_copy_into(rtp_audio_buf,
                       rtpmoonlightpay->packets_buffer[rtpmoonlightpay->cur_seq_number % AUDIO_DATA_SHARDS]);

  // Time to generate FEC based on the previous payloads
  if (time_to_fec) {
    auto rs = reed_solomon_new(AUDIO_DATA_SHARDS, AUDIO_FEC_SHARDS);

    memcpy(&rs->m[16], AUDIO_FEC_PARITY, sizeof(AUDIO_FEC_PARITY));
    memcpy(rs->parity, AUDIO_FEC_PARITY, sizeof(AUDIO_FEC_PARITY));

    auto encoded_block_size = (int)gst_buffer_get_size(rtp_audio_buf);
    reed_solomon_encode(rs, rtpmoonlightpay->packets_buffer.data(), AUDIO_TOTAL_SHARDS, encoded_block_size);

    for (auto fec_packet_idx = 0; fec_packet_idx < AUDIO_FEC_SHARDS; fec_packet_idx++) {
      auto fec_packet_header = create_rtp_fec_header(*rtpmoonlightpay, fec_packet_idx);

      GstBuffer *fec_payload_buf = gst_buffer_new_allocate(nullptr, encoded_block_size, nullptr);
      gst_buffer_fill(fec_payload_buf,
                      sizeof(state::AudioRTPHeaders),
                      rtpmoonlightpay->packets_buffer[AUDIO_DATA_SHARDS + fec_packet_idx],
                      encoded_block_size - sizeof(state::AudioRTPHeaders));

      auto fec_buf = gst_buffer_append(fec_packet_header, fec_payload_buf);
      gst_copy_timestamps(inbuf, fec_buf);

      gst_buffer_list_insert(rtp_packets, -1, fec_buf);
    }
  }
  rtpmoonlightpay->cur_seq_number++;

  return rtp_packets;
}
} // namespace audio