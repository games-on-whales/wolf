#pragma once

#include <gst-plugin/gstrtpmoonlightpay_audio.hpp>
#include <gst-plugin/utils.hpp>
#include <helpers/logger.hpp>
#include <moonlight/data-structures.hpp>

namespace audio {

struct AudioRTPHeaders {
  moonlight::RTP_PACKET rtp;
};

struct AudioFECHeader {
  uint8_t fecShardIndex;
  uint8_t payloadType;
  uint16_t baseSequenceNumber;
  uint32_t baseTimestamp;
  uint32_t ssrc;
};

struct AudioFECPacket {
  moonlight::RTP_PACKET rtp;
  AudioFECHeader fec_header;
};

constexpr auto RTP_HEADER_SIZE = sizeof(AudioRTPHeaders);
constexpr auto FEC_HEADER_SIZE = sizeof(AudioFECPacket);

/**
 * Creates an RTP header and returns a GstBuffer to it
 */
static GstBuffer *create_rtp_header(const gst_rtp_moonlight_pay_audio &rtpmoonlightpay) {
  GstBuffer *buf = gst_buffer_new_and_fill(RTP_HEADER_SIZE, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (AudioRTPHeaders *)info.data;

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
  GstBuffer *buf = gst_buffer_new_and_fill(FEC_HEADER_SIZE, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (AudioFECPacket *)info.data;

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

static GstBuffer *create_rtp_audio_buffer(const gst_rtp_moonlight_pay_audio &rtpmoonlightpay, GstBuffer *inbuf) {
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

  GstBufferList *rtp_packets = gst_buffer_list_new();

  auto rtp_audio_buf = create_rtp_audio_buffer(*rtpmoonlightpay, inbuf);
  gst_buffer_list_add(rtp_packets, rtp_audio_buf);

  // save the payload locally
  gst_buffer_copy_into(rtp_audio_buf,
                       rtpmoonlightpay->packets_buffer[rtpmoonlightpay->cur_seq_number % AUDIO_DATA_SHARDS]);

  // Time to generate FEC based on the previous payloads
  if (time_to_fec) {
    /* Here the assumption is that all audio blocks will have the exact same size */
    auto rtp_block_size = (int)gst_buffer_get_size(rtp_audio_buf);
    auto payload_size = rtp_block_size - RTP_HEADER_SIZE;
    if (moonlight::fec::encode(rtpmoonlightpay->rs.get(),
                               rtpmoonlightpay->packets_buffer,
                               AUDIO_TOTAL_SHARDS,
                               rtp_block_size) != 0) {
      logs::log(logs::warning, "Error during audio FEC encoding");
    }

    for (auto fec_packet_idx = 0; fec_packet_idx < AUDIO_FEC_SHARDS; fec_packet_idx++) {
      auto fec_packet = create_rtp_fec_header(*rtpmoonlightpay, fec_packet_idx);

      GstBuffer *fec_payload_buf = gst_buffer_new_allocate(nullptr, payload_size, nullptr);
      gst_buffer_fill(fec_payload_buf,
                      0,
                      rtpmoonlightpay->packets_buffer[AUDIO_DATA_SHARDS + fec_packet_idx] + RTP_HEADER_SIZE,
                      payload_size);

      fec_packet = gst_buffer_append(fec_packet, fec_payload_buf);
      gst_buffer_list_add(rtp_packets, fec_packet);
    }
  }
  rtpmoonlightpay->cur_seq_number++;

  return rtp_packets;
}
} // namespace audio