#pragma once
#include <boost/endian/conversion.hpp>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <streaming/data-structures.hpp>

/**
 * Creates a GstBuffer and fill the memory with the given value
 */
static GstBuffer *gst_buffer_new_and_fill(gsize size, int fill_val) {
  GstBuffer *buf = gst_buffer_new_allocate(NULL, size, NULL);

  /* get WRITE access to the memory and fill with fill_val */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);
  memset(info.data, fill_val, info.size);
  gst_buffer_unmap(buf, &info);
  return buf;
}

/**
 * Creates a GstBuffer from the given array of chars
 */
static GstBuffer *gst_buffer_new_and_fill(gsize size, const char vals[]) {
  GstBuffer *buf = gst_buffer_new_allocate(NULL, size, NULL);

  /* get WRITE access to the memory and fill with vals */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);
  for (int i = 0; i < size; i++) {
    info.data[i] = vals[i];
  }
  gst_buffer_unmap(buf, &info);
  return buf;
}

/**
 * From a list of buffer returns a single buffer that contains them all.
 * No copy of the stored data is done
 */
static GstBuffer *gst_buffer_list_unfold(GstBufferList *buffer_list) {
  GstBuffer *buf = gst_buffer_new_allocate(NULL, 0, NULL);

  for (int idx = 0; idx < gst_buffer_list_length(buffer_list); idx++) {
    auto buf_idx =
        gst_buffer_copy(gst_buffer_list_get(buffer_list, idx)); // copy here is about the buffer object, not the data
    gst_buffer_append(buf, buf_idx);
  }

  return buf;
}

/**
 * Copies out buffer metadata without affecting data
 */
static void gst_copy_timestamps(GstBuffer *src, GstBuffer *dest) {
  dest->pts = src->pts;
  dest->dts = src->dts;
  dest->offset = src->offset;
  dest->duration = src->duration;
  dest->offset_end = src->offset_end;
}

/**
 * Creates an RTP header and returns a GstBuffer to it
 */
static GstBuffer *create_rtp_header(const gst_rtp_moonlight_pay &rtpmoonlightpay, int packet_nr, int tot_packets) {
  constexpr auto rtp_header_size = sizeof(state::VideoRTPHeaders);
  GstBuffer *buf = gst_buffer_new_and_fill(rtp_header_size, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (state::VideoRTPHeaders *)info.data;

  packet->rtp.header = 0x80 | FLAG_EXTENSION;
  packet->rtp.packetType = 0x00;
  packet->rtp.timestamp = 0x00;
  packet->rtp.ssrc = 0x00;

  uint16_t sequence_number = rtpmoonlightpay.cur_seq_number + packet_nr;
  packet->rtp.sequenceNumber = boost::endian::native_to_big(sequence_number);

  packet->packet.frameIndex = rtpmoonlightpay.frame_num;
  packet->packet.streamPacketIndex = ((uint32_t)sequence_number) << 8;

  packet->packet.multiFecFlags = 0x10;
  packet->packet.multiFecBlocks = 0;
  packet->packet.fecInfo = (packet_nr << 12 | tot_packets << 22 | 0 << 4);

  packet->packet.flags = FLAG_CONTAINS_PIC_DATA;

  if (packet_nr == 0) {
    packet->packet.flags |= FLAG_SOF;
  }

  if (packet_nr == tot_packets - 1) {
    packet->packet.flags |= FLAG_EOF;
  }

  gst_buffer_unmap(buf, &info);

  return buf;
}

GstBuffer *prepend_video_header(GstBuffer *inbuf) {
  constexpr auto video_payload_header_size = 8;
  GstBuffer *video_header = gst_buffer_new_and_fill(video_payload_header_size, "\0017charss");
  auto full_payload_buf = gst_buffer_append(video_header, inbuf);
  gst_copy_timestamps(inbuf, full_payload_buf);
  return full_payload_buf;
}

/**
 * Split the input buffer into packets, will prepend the RTP header and append any padding if needed
 */
static GstBufferList *generate_rtp_packets(const gst_rtp_moonlight_pay &rtpmoonlightpay, GstBuffer *inbuf) {
  auto in_buf_size = gst_buffer_get_size(inbuf);
  auto payload_size = rtpmoonlightpay.payload_size;
  auto tot_packets = (in_buf_size / payload_size) + 1;
  GstBufferList *buffers = gst_buffer_list_new_sized(tot_packets);

  for (int packet_nr = 0; packet_nr < tot_packets; packet_nr++) {
    auto begin = packet_nr * payload_size;
    auto remaining = in_buf_size - begin;
    auto packet_payload_size = MIN(remaining, payload_size);

    GstBuffer *header = create_rtp_header(rtpmoonlightpay, packet_nr, tot_packets);

    GstBuffer *payload = gst_buffer_copy_region(inbuf, GST_BUFFER_COPY_ALL, begin, packet_payload_size);
    GstBuffer *rtp_packet = gst_buffer_append(header, payload);

    if ((remaining < payload_size) && rtpmoonlightpay.add_padding) {
      GstBuffer *padding = gst_buffer_new_and_fill(payload_size - remaining, 0x00);
      gst_buffer_append(rtp_packet, padding);
    }

    gst_copy_timestamps(inbuf, rtp_packet);
    gst_buffer_list_insert(buffers, -1, rtp_packet);
  }

  return buffers;
}

/**
 * Given the RTP packets that contains payload,
 * will generate extra RTP packets with the FEC information.
 *
 * Returns a list of GstBuffer that contains the input rtp_packets + the newly created FEC packets.
 * Will also add FEC info to the RTP headers of the original packets
 */
static GstBufferList *
generate_fec_packets(const gst_rtp_moonlight_pay &rtpmoonlightpay, GstBufferList *rtp_packets, GstBuffer *inbuf) {
  GstMapInfo info;
  GstBuffer *rtp_payload = gst_buffer_list_unfold(rtp_packets);

  auto payload_size = (int)gst_buffer_get_size(rtp_payload);
  auto blocksize = rtpmoonlightpay.payload_size + (int)sizeof(state::VideoRTPHeaders);
  auto needs_padding = payload_size % blocksize != 0;

  auto fec_percentage = rtpmoonlightpay.fec_percentage;
  auto data_shards = payload_size / blocksize + (needs_padding ? 1 : 0);
  auto parity_shards = (data_shards * fec_percentage + 99) / 100;

  // increase the FEC percentage in order to get the min required packets
  if (parity_shards < rtpmoonlightpay.min_required_fec_packets) {
    parity_shards = rtpmoonlightpay.min_required_fec_packets;
    fec_percentage = (100 * parity_shards) / data_shards;
  }

  auto nr_shards = data_shards + parity_shards;

  // pads rtp_payload to blocksize
  if (needs_padding) {
    GstBuffer *pad = gst_buffer_new_and_fill((data_shards * blocksize) - payload_size, 0x00);
    rtp_payload = gst_buffer_append(rtp_payload, pad);
  }

  rtp_payload = gst_buffer_append(rtp_payload, gst_buffer_new_and_fill((parity_shards * blocksize), 0x00));
  gst_buffer_map(rtp_payload, &info, GST_MAP_WRITE);

  // Reed Solomon encode the full stream of bytes
  auto rs = reed_solomon_new(data_shards, parity_shards);
  unsigned char *ptr[nr_shards];
  for (int shard_idx = 0; shard_idx < nr_shards; shard_idx++) {
    ptr[shard_idx] = info.data + (shard_idx * blocksize);
  }
  reed_solomon_encode(rs, ptr, nr_shards, blocksize);
  reed_solomon_release(rs);

  // Split out back into RTP packets and update FEC info
  GstBufferList *buffers = gst_buffer_list_new_sized(nr_shards);
  for (int shard_idx = 0; shard_idx < nr_shards; shard_idx++) {
    auto position = shard_idx * blocksize;
    auto rtp_packet = (state::VideoRTPHeaders *)(info.data + position);

    rtp_packet->packet.frameIndex = rtpmoonlightpay.frame_num;

    rtp_packet->packet.fecInfo = (shard_idx << 12 | data_shards << 22 | fec_percentage << 4);
    rtp_packet->packet.multiFecBlocks = 0; // TODO: support multiple fec blocks?
    rtp_packet->packet.multiFecFlags = 0x10;

    rtp_packet->rtp.header = 0x80 | FLAG_EXTENSION;
    uint16_t sequence_number = rtpmoonlightpay.cur_seq_number + shard_idx;
    rtp_packet->rtp.sequenceNumber = boost::endian::native_to_big(sequence_number);

    GstBuffer *packet_buf = gst_buffer_new_allocate(nullptr, blocksize, nullptr);
    gst_buffer_fill(packet_buf, 0, rtp_packet, blocksize);
    gst_copy_timestamps(inbuf, packet_buf);
    gst_buffer_list_insert(buffers, -1, packet_buf);
  }

  gst_buffer_unmap(rtp_payload, &info);

  return buffers;
}

/**
 * Our main function:
 * Given an input buffer containing some kind of payload
 * split it in one or multiple RTP packets following the Moonlight specification.
 *
 * @return a list of buffers, each element representing a single RTP packet
 */
static GstBufferList *split_into_rtp(gst_rtp_moonlight_pay *rtpmoonlightpay, GstBuffer *inbuf) {
  auto full_payload_buf = inbuf;
  if (rtpmoonlightpay->stream_type == VIDEO) {
    full_payload_buf = prepend_video_header(inbuf);
  }

  GstBufferList *rtp_packets = generate_rtp_packets(*rtpmoonlightpay, full_payload_buf);
  if (rtpmoonlightpay->fec_percentage > 0) {
    rtp_packets = generate_fec_packets(*rtpmoonlightpay, rtp_packets, full_payload_buf);
  }
  rtpmoonlightpay->cur_seq_number += (int)gst_buffer_list_length(rtp_packets);
  rtpmoonlightpay->frame_num++;

  return rtp_packets;
}