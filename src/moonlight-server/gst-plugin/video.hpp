#pragma once
#include <algorithm>
#include <boost/endian.hpp>
#include <cmath>
#include <gst-plugin/gstrtpmoonlightpay_video.hpp>
#include <gst-plugin/utils.hpp>
#include <helpers/logger.hpp>
#include <moonlight/data-structures.hpp>

namespace gst_moonlight_video {

struct VideoRTPHeaders {
  // headers
  moonlight::RTP_PACKET rtp;
  char reserved[4];
  moonlight::NV_VIDEO_PACKET packet;
};

#pragma pack(push, 1)
struct VideoShortHeader {
  uint8_t header_type; // Always 0x01 for short headers
  uint8_t unknown[2];
  // Currently known values:
  // 1 = Normal P-frame
  // 2 = IDR-frame
  // 4 = P-frame with intra-refresh blocks
  // 5 = P-frame after reference frame invalidation
  uint8_t frame_type;

  // Length of the final packet payload for codecs that cannot handle
  // zero padding, such as AV1 (Sunshine extension).
  boost::endian::little_uint16_at last_payload_len;

  uint8_t unknown2[2];
};
#pragma pack(pop)

/**
 * Creates an RTP header and returns a GstBuffer to it
 */
static GstBuffer *
create_rtp_header(const gst_rtp_moonlight_pay_video &rtpmoonlightpay, int packet_nr, int tot_packets) {
  constexpr auto rtp_header_size = sizeof(VideoRTPHeaders);
  GstBuffer *buf = gst_buffer_new_and_fill(rtp_header_size, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (VideoRTPHeaders *)info.data;

  packet->rtp.header = 0x80 | FLAG_EXTENSION;
  packet->rtp.packetType = 0x00;
  packet->rtp.timestamp = 0x00;
  packet->rtp.ssrc = 0x00;

  uint32_t sequence_number = rtpmoonlightpay.cur_seq_number + packet_nr;
  packet->rtp.sequenceNumber = boost::endian::native_to_big((uint16_t)sequence_number);

  packet->packet.frameIndex = rtpmoonlightpay.frame_num;
  packet->packet.streamPacketIndex = sequence_number << 8;

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

static GstBuffer *prepend_video_header(const gst_rtp_moonlight_pay_video &rtpmoonlightpay, GstBuffer *inbuf) {
  constexpr auto video_payload_header_size = 8;
  auto in_buf_size = gst_buffer_get_size(inbuf);
  GstBuffer *video_header = gst_buffer_new_and_fill(video_payload_header_size, 0x00);
  bool is_key = !GST_BUFFER_FLAG_IS_SET(inbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (is_key) {
    logs::log(logs::trace, "[GStreamer] KEYFRAME!");
  }

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(video_header, &info, GST_MAP_WRITE);

  /* set headers */
  auto packet = (VideoShortHeader *)info.data;
  packet->header_type = 0x01;
  packet->frame_type = is_key ? 0x02 : 0x01;
  packet->last_payload_len = (in_buf_size + video_payload_header_size) %
                             (rtpmoonlightpay.payload_size - sizeof(moonlight::NV_VIDEO_PACKET));
  if (packet->last_payload_len == 0) {
    packet->last_payload_len = rtpmoonlightpay.payload_size - sizeof(moonlight::NV_VIDEO_PACKET);
  }

  gst_buffer_unmap(video_header, &info);

  auto full_payload_buf = gst_buffer_append(video_header, gst_buffer_ref(inbuf));
  return full_payload_buf;
}

/**
 * Split the input buffer into packets, will prepend the RTP header and append any padding if needed
 */
static GstBufferList *generate_rtp_packets(const gst_rtp_moonlight_pay_video &rtpmoonlightpay, GstBuffer *inbuf) {
  auto in_buf_size = gst_buffer_get_size(inbuf);
  auto payload_size = rtpmoonlightpay.payload_size - MAX_RTP_HEADER_SIZE;
  auto tot_packets = std::ceil((float)in_buf_size / payload_size);
  GstBufferList *buffers = gst_buffer_list_new();

  for (int packet_nr = 0; packet_nr < tot_packets; packet_nr++) {
    auto begin = packet_nr * payload_size;
    auto remaining = in_buf_size - begin;
    auto packet_payload_size = MIN(remaining, payload_size);

    GstBuffer *rtp_packet = create_rtp_header(rtpmoonlightpay, packet_nr, tot_packets);

    GstBuffer *payload = gst_buffer_copy_region(inbuf, GST_BUFFER_COPY_ALL, begin, packet_payload_size);
    rtp_packet = gst_buffer_append(rtp_packet, payload);

    if ((remaining < payload_size) && rtpmoonlightpay.add_padding) {
      GstBuffer *padding = gst_buffer_new_and_fill(payload_size - remaining, 0x00);
      rtp_packet = gst_buffer_append(rtp_packet, padding);
    }

    gst_copy_timestamps(inbuf, rtp_packet);
    gst_buffer_list_add(buffers, rtp_packet);
  }

  return buffers;
}

static void update_fec_info(const gst_rtp_moonlight_pay_video &rtpmoonlightpay,
                            VideoRTPHeaders *rtp_packet,
                            int shard_idx,
                            int data_shards,
                            int fec_percentage,
                            int block_index = 0,
                            int last_block_index = 0) {
  rtp_packet->packet.frameIndex = rtpmoonlightpay.frame_num;

  rtp_packet->packet.fecInfo = (shard_idx << 12 | data_shards << 22 | fec_percentage << 4);
  rtp_packet->packet.multiFecBlocks = (block_index << 4) | last_block_index;
  rtp_packet->packet.multiFecFlags = 0x10;

  rtp_packet->rtp.header = 0x80 | FLAG_EXTENSION;
  uint32_t sequence_number = rtpmoonlightpay.cur_seq_number + shard_idx;
  rtp_packet->rtp.sequenceNumber = boost::endian::native_to_big((uint16_t)sequence_number);
}

struct BLOCKS {
  int block_size;
  int data_shards;
  int parity_shards;
  int fec_percentage;
};

static BLOCKS determine_split(const gst_rtp_moonlight_pay_video &rtpmoonlightpay, int data_shards) {
  auto blocksize = rtpmoonlightpay.payload_size + (int)sizeof(VideoRTPHeaders) - MAX_RTP_HEADER_SIZE;
  auto fec_percentage = rtpmoonlightpay.fec_percentage;
  int parity_shards = (data_shards * fec_percentage + 99) / 100;

  // increase the FEC percentage in order to get the min required packets
  if (parity_shards < rtpmoonlightpay.min_required_fec_packets) {
    parity_shards = rtpmoonlightpay.min_required_fec_packets;
    fec_percentage = (100 * parity_shards) / data_shards;
  }

  return {.block_size = blocksize,
          .data_shards = data_shards,
          .parity_shards = parity_shards,
          .fec_percentage = fec_percentage};
}

/**
 * Maximum number of FEC blocks a single video frame can be split into.
 *
 * `multiFecBlocks` stores both the current block index and the last block index in 2 bits each
 * (moonlight-common-c `RtpVideoQueue.c` reads `(multiFecBlocks >> 4) & 0x3` and `(multiFecBlocks >> 6) & 0x3`),
 * so a frame can carry at most 4 FEC blocks.
 */
constexpr int MAX_FEC_BLOCKS = 4;

/**
 * Computes how many data shards a single FEC block can hold so that data + parity shards
 * stay within the Reed-Solomon limit (DATA_SHARDS_MAX).
 *
 * Starts from the upper bound `DATA_SHARDS_MAX * 100 / (100 + fec_percentage)` and walks down,
 * reusing determine_split() so that the `min_required_fec_packets` bump is honoured.
 *
 * @param rtpmoonlightpay the payloader holding `fec_percentage` and `min_required_fec_packets`
 * @return the largest number of data shards per block that fits, or 0 if not even a single data shard fits
 *
 * Example: with fec_percentage = 20 and min_required_fec_packets = 2 this returns 212 (212 + 43 = 255).
 */
static int max_data_shards_per_fec_block(const gst_rtp_moonlight_pay_video &rtpmoonlightpay) {
  int data_shards = DATA_SHARDS_MAX * 100 / (100 + rtpmoonlightpay.fec_percentage);
  while (data_shards > 0) {
    auto split = determine_split(rtpmoonlightpay, data_shards);
    if (split.data_shards + split.parity_shards <= DATA_SHARDS_MAX) {
      break;
    }
    data_shards--;
  }
  return data_shards;
}

/**
 * How a frame is split into FEC blocks.
 *
 * `nr_blocks` is between 1 and MAX_FEC_BLOCKS; every packet of the frame advertises the same block count.
 * When `with_fec` is false the blocks carry no parity packets (0% FEC) but still use consistent headers.
 */
struct FEC_LAYOUT {
  int nr_blocks;
  bool with_fec;
};

/**
 * Decides how many FEC blocks a frame of `data_shards` packets needs and whether they can carry parity.
 *
 *  - up to 90 data shards: a single block, with FEC only if the block fits in DATA_SHARDS_MAX
 *  - otherwise: at least 3 blocks (the historical split), up to MAX_FEC_BLOCKS when bigger frames need it
 *  - frames that don't fit even in MAX_FEC_BLOCKS blocks are sent as MAX_FEC_BLOCKS blocks without FEC
 *
 * Frames above MAX_FEC_BLOCKS * 1023 data packets would overflow the 10-bit `fecInfo` fields; that is
 * several MB per frame and is not handled here.
 *
 * @param rtpmoonlightpay the payloader holding the FEC settings
 * @param data_shards the number of data packets in the frame
 * @return the block layout to use for this frame
 *
 * Example: with fec_percentage = 20, 636 data shards -> {3, true}, 640 -> {4, true}, 849 -> {4, false}
 */
static FEC_LAYOUT determine_fec_layout(const gst_rtp_moonlight_pay_video &rtpmoonlightpay, int data_shards) {
  auto max_data_shards = max_data_shards_per_fec_block(rtpmoonlightpay);
  if (data_shards <= 90) {
    return {.nr_blocks = 1, .with_fec = max_data_shards >= data_shards};
  }

  auto needed_blocks = max_data_shards > 0 ? (data_shards + max_data_shards - 1) / max_data_shards : MAX_FEC_BLOCKS + 1;
  if (needed_blocks <= MAX_FEC_BLOCKS) {
    return {.nr_blocks = std::max(3, needed_blocks), .with_fec = true};
  }
  return {.nr_blocks = MAX_FEC_BLOCKS, .with_fec = false};
}

/**
 * Given the RTP packets that contains payload,
 * will generate extra RTP packets with the FEC information.
 *
 * Will modify the input rtp_packets with the correct FEC info
 * and will append the FEC packets at the end
 */
static void generate_fec_packets(const gst_rtp_moonlight_pay_video &rtpmoonlightpay,
                                 GstBufferList *rtp_packets,
                                 GstBuffer *inbuf,
                                 int block_index = 0,
                                 int last_block_index = 0) {
  GstMapInfo info;
  GstBuffer *rtp_payload = gst_buffer_list_unfold(rtp_packets);

  auto payload_size = (int)gst_buffer_get_size(rtp_payload);
  auto blocks = determine_split(rtpmoonlightpay, gst_buffer_list_length(rtp_packets));
  const auto nr_shards = blocks.data_shards + blocks.parity_shards;

  // pads rtp_payload to blocksize
  if (payload_size % blocks.block_size != 0) {
    GstBuffer *pad = gst_buffer_new_and_fill((blocks.data_shards * blocks.block_size) - payload_size, 0x00);
    rtp_payload = gst_buffer_append(rtp_payload, pad);
  }

  // Allocate space for FEC packets
  auto fec_buff = gst_buffer_new_and_fill((blocks.parity_shards * blocks.block_size), 0x00);
  rtp_payload = gst_buffer_append(rtp_payload, fec_buff);
  gst_buffer_map(rtp_payload, &info, GST_MAP_WRITE);

  // Reed Solomon encode the full stream of bytes
  auto rs = moonlight::fec::create(blocks.data_shards, blocks.parity_shards);
  std::vector<unsigned char *> ptr(nr_shards);
  for (int shard_idx = 0; shard_idx < nr_shards; shard_idx++) {
    ptr[shard_idx] = info.data + (shard_idx * blocks.block_size);
  }
  if (moonlight::fec::encode(rs.get(), &ptr.front(), nr_shards, blocks.block_size) != 0) {
    logs::log(logs::warning, "Error during video FEC encoding");
  }

  // update FEC info of the already created RTP packets
  for (int shard_idx = 0; shard_idx < blocks.data_shards; shard_idx++) {
    GstMapInfo data_info;
    auto data_pkt = gst_buffer_list_get(rtp_packets, shard_idx);
    gst_buffer_map(data_pkt, &data_info, GST_MAP_WRITE);

    update_fec_info(rtpmoonlightpay,
                    (VideoRTPHeaders *)(data_info.data),
                    shard_idx,
                    blocks.data_shards,
                    blocks.fec_percentage,
                    block_index,
                    last_block_index);
    gst_copy_timestamps(inbuf, data_pkt);
    gst_buffer_unmap(data_pkt, &data_info);
  }

  // Push back the newly created RTP packets with the FEC info
  for (int shard_idx = blocks.data_shards; shard_idx < nr_shards; shard_idx++) {
    auto position = shard_idx * blocks.block_size;
    auto rtp_packet = (VideoRTPHeaders *)(info.data + position);

    update_fec_info(rtpmoonlightpay,
                    rtp_packet,
                    shard_idx,
                    blocks.data_shards,
                    blocks.fec_percentage,
                    block_index,
                    last_block_index);

    GstBuffer *packet_buf = gst_buffer_new_allocate(nullptr, blocks.block_size, nullptr);
    gst_buffer_fill(packet_buf, 0, rtp_packet, blocks.block_size);
    gst_copy_timestamps(inbuf, packet_buf);
    gst_buffer_list_add(rtp_packets, packet_buf);
  }

  gst_buffer_unmap(rtp_payload, &info);
  gst_buffer_unref(rtp_payload);
}

/**
 * Labels the RTP packets of a block as "N data shards, 0% FEC" without appending any parity packet.
 *
 * Used when a frame is too big to carry FEC even when split in MAX_FEC_BLOCKS blocks: every packet still
 * gets a consistent multi-FEC description, and the client completes the block once all of its data packets
 * arrive (`reconstructFrame` returns early when `receivedDataPackets == bufferDataPackets`).
 *
 * @param rtpmoonlightpay the payloader, used for the frame number and the current sequence number
 * @param rtp_packets the data packets of this block; modified in place
 * @param inbuf the source buffer, used to copy timestamps
 * @param block_index the 0-based index of this block (update_fec_info shifts it into place)
 * @param last_block_index the last block index, already shifted (`<< 6`)
 */
static void set_fec_info_without_parity(const gst_rtp_moonlight_pay_video &rtpmoonlightpay,
                                        GstBufferList *rtp_packets,
                                        GstBuffer *inbuf,
                                        int block_index,
                                        int last_block_index) {
  auto data_shards = (int)gst_buffer_list_length(rtp_packets);
  for (int shard_idx = 0; shard_idx < data_shards; shard_idx++) {
    GstMapInfo data_info;
    auto data_pkt = gst_buffer_list_get(rtp_packets, shard_idx);
    gst_buffer_map(data_pkt, &data_info, GST_MAP_WRITE);

    update_fec_info(rtpmoonlightpay,
                    (VideoRTPHeaders *)(data_info.data),
                    shard_idx,
                    data_shards,
                    0,
                    block_index,
                    last_block_index);
    gst_copy_timestamps(inbuf, data_pkt);
    gst_buffer_unmap(data_pkt, &data_info);
  }
}

/**
 * Given a list of RTP packets will split them in `layout.nr_blocks` blocks (1-4) of:
 * [Payloads + FEC], [Payloads + FEC], ...
 * When `layout.with_fec` is false the blocks only contain the payloads, labelled with 0% FEC.
 *
 * Returns a new linear list of all the blocks
 * Will modify the input rtp_packets with the correct FEC info
 */
static GstBufferList *generate_fec_multi_blocks(gst_rtp_moonlight_pay_video *rtpmoonlightpay,
                                                GstBufferList *rtp_packets,
                                                const FEC_LAYOUT &layout,
                                                GstBuffer *inbuf) {
  auto rtp_packets_size = gst_buffer_list_length(rtp_packets);

  const auto nr_blocks = layout.nr_blocks;
  const auto last_block_index = (nr_blocks - 1) << 6;

  GstBufferList *final_packets = gst_buffer_list_new(); // we'll increase the size on each block iteration

  auto packets_per_block = (int)std::ceil((float)rtp_packets_size / nr_blocks);
  for (int block_idx = 0; block_idx < nr_blocks; block_idx++) {
    auto list_start = block_idx * packets_per_block;
    auto list_end = MIN((block_idx + 1) * packets_per_block, rtp_packets_size);
    auto block_packets = gst_buffer_list_sub(rtp_packets, list_start, list_end);

    // bear in mind that since no actual data copy is done,
    // this will also modify the FEC information in the original rtp_packets list
    if (layout.with_fec) {
      generate_fec_packets(*rtpmoonlightpay, block_packets, inbuf, block_idx, last_block_index);
    } else {
      set_fec_info_without_parity(*rtpmoonlightpay, block_packets, inbuf, block_idx, last_block_index);
    }

    // We have to copy out the additional FEC packets; we just put them all back into a new linear list
    auto total_block_packets = gst_buffer_list_length(block_packets);
    for (int packet_idx = 0; packet_idx < total_block_packets; packet_idx++) {
      // copy here is about the buffer object, not the data
      gst_buffer_list_add(final_packets, gst_buffer_copy(gst_buffer_list_get(block_packets, packet_idx)));
    }

    // This will adjust the sequenceNumber of the RTP packet
    rtpmoonlightpay->cur_seq_number += total_block_packets;
    gst_buffer_list_unref(block_packets);
  }

  gst_buffer_list_unref(rtp_packets);

  return final_packets;
}

/**
 * Our main function:
 * Given an input buffer containing some kind of payload
 * split it in one or multiple RTP packets following the Moonlight specification.
 *
 * @return a list of buffers, each element representing a single RTP packet
 */
static GstBufferList *split_into_rtp(gst_rtp_moonlight_pay_video *rtpmoonlightpay, GstBuffer *inbuf) {
  auto full_payload_buf = prepend_video_header(*rtpmoonlightpay, inbuf);

  GstBufferList *rtp_packets = generate_rtp_packets(*rtpmoonlightpay, full_payload_buf);

  if (rtpmoonlightpay->fec_percentage > 0) {
    auto rtp_packets_size = (int)gst_buffer_list_length(rtp_packets);
    auto layout = determine_fec_layout(*rtpmoonlightpay, rtp_packets_size);
    if (!layout.with_fec) {
      logs::log(logs::warning,
                "[GSTREAMER] Frame of {} packets does not fit in {} FEC blocks; sending it without FEC",
                rtp_packets_size,
                MAX_FEC_BLOCKS);
    }

    // Every packet of the frame must share the same block layout, see determine_fec_layout()
    if (layout.nr_blocks > 1) {
      rtp_packets = generate_fec_multi_blocks(rtpmoonlightpay, rtp_packets, layout, inbuf);
    } else {
      if (layout.with_fec) {
        generate_fec_packets(*rtpmoonlightpay, rtp_packets, inbuf, 0, 0);
      }
      rtpmoonlightpay->cur_seq_number += gst_buffer_list_length(rtp_packets);
    }
  }

  rtpmoonlightpay->frame_num++;
  gst_buffer_unref(full_payload_buf);
  return rtp_packets;
}

} // namespace gst_moonlight_video