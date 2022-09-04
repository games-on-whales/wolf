/**
 * SECTION:element-gstrtpmoonlightpay
 *
 * The rtpmoonlightpay element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v fakesrc ! rtpmoonlightpay ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

extern "C" {
#include <moonlight-common-c/reedsolomon/rs.h>
}

#include "gstrtpmoonlightpay.hpp"
#include "utils.hpp"
#include <boost/endian/conversion.hpp>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <streaming/data-structures.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_rtp_moonlight_pay_debug_category);
#define GST_CAT_DEFAULT gst_rtp_moonlight_pay_debug_category

/* prototypes */

static void
gst_rtp_moonlight_pay_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_rtp_moonlight_pay_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_rtp_moonlight_pay_dispose(GObject *object);
static void gst_rtp_moonlight_pay_finalize(GObject *object);

static GstFlowReturn gst_rtp_moonlight_pay_generate_output(GstBaseTransform *trans, GstBuffer **outbuf);

enum {
  /**
   * Maximum size of RTP packets. If a video payload surpasses this it'll be split in multiple packets
   */
  PROP_PAYLOAD_SIZE = 19,

  /**
   * If TRUE will add padding for packets that have a payload < payload_size
   */
  PROP_ADD_PADDING = 20,

  /**
   * Percentage of video payload to be encoded for Forward Error Correction
   */
  PROP_FEC_PERCENTAGE = 21,

  /**
   * Minimum number of FEC packages required by Moonlight
   */
  PROP_MIN_REQUIRED_FEC_PACKETS = 22
};

/* pad templates */

static GstStaticPadTemplate gst_rtp_moonlight_pay_src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

static GstStaticPadTemplate gst_rtp_moonlight_pay_sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(gst_rtp_moonlight_pay,
                        gst_rtp_moonlight_pay,
                        GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_rtp_moonlight_pay_debug_category,
                                                "rtpmoonlightpay",
                                                0,
                                                "debug category for rtpmoonlightpay element"));

static void gst_rtp_moonlight_pay_class_init(gst_rtp_moonlight_payClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &gst_rtp_moonlight_pay_src_template);
  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &gst_rtp_moonlight_pay_sink_template);

  gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
                                        "FIXME Long name",
                                        "Generic",
                                        "FIXME Description",
                                        "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_rtp_moonlight_pay_set_property;
  gobject_class->get_property = gst_rtp_moonlight_pay_get_property;

  g_object_class_install_property(
      gobject_class,
      PROP_PAYLOAD_SIZE,
      g_param_spec_int(
          "payload_size",
          "payload_size",
          "Maximum size of RTP packets. If a video payload surpasses this it'll be split in multiple packets",
          0,
          10240,
          1024,
          G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class,
      PROP_ADD_PADDING,
      g_param_spec_boolean("add_padding",
                           "add_padding",
                           "If TRUE will add padding for packets that have a payload < payload_size",
                           TRUE,
                           G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class,
      PROP_FEC_PERCENTAGE,
      g_param_spec_int("fec_percentage",
                       "fec_percentage",
                       "Percentage of video payload to be encoded for Forward Error Correction",
                       0,
                       100,
                       20,
                       G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_MIN_REQUIRED_FEC_PACKETS,
                                  g_param_spec_int("min_required_fec_packets",
                                                   "min_required_fec_packets",
                                                   "Minimum number of FEC packages required by Moonlight",
                                                   0,
                                                   255,
                                                   2,
                                                   G_PARAM_READWRITE));

  gobject_class->dispose = gst_rtp_moonlight_pay_dispose;
  gobject_class->finalize = gst_rtp_moonlight_pay_finalize;

  base_transform_class->generate_output = GST_DEBUG_FUNCPTR(gst_rtp_moonlight_pay_generate_output);
}

static void gst_rtp_moonlight_pay_init(gst_rtp_moonlight_pay *rtpmoonlightpay) {
  rtpmoonlightpay->payload_size = 1008;
  rtpmoonlightpay->add_padding = true;

  rtpmoonlightpay->fec_percentage = 20;
  rtpmoonlightpay->min_required_fec_packets = 2;

  rtpmoonlightpay->cur_seq_number = 0;
  rtpmoonlightpay->frame_num = 0;
}

void gst_rtp_moonlight_pay_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  gst_rtp_moonlight_pay *rtpmoonlightpay = gst_rtp_moonlight_pay(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay, "set_property");

  switch (property_id) {
  case PROP_PAYLOAD_SIZE:
    rtpmoonlightpay->payload_size = g_value_get_int(value);
    //    g_print("payload_size was changed to %d\n", rtpmoonlightpay->payload_size);
    break;
  case PROP_ADD_PADDING:
    rtpmoonlightpay->add_padding = g_value_get_boolean(value);
    //    g_print("add_padding was changed to %d\n", rtpmoonlightpay->add_padding);
    break;
  case PROP_FEC_PERCENTAGE:
    rtpmoonlightpay->fec_percentage = g_value_get_int(value);
    //    g_print("fec_percentage was changed to %d\n", rtpmoonlightpay->fec_percentage);
    break;
  case PROP_MIN_REQUIRED_FEC_PACKETS:
    rtpmoonlightpay->min_required_fec_packets = g_value_get_int(value);
    //    g_print("min_required_fec_packets was changed to %d\n", rtpmoonlightpay->min_required_fec_packets);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_rtp_moonlight_pay_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  gst_rtp_moonlight_pay *rtpmoonlightpay = gst_rtp_moonlight_pay(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay, "get_property");

  switch (property_id) {
  case PROP_PAYLOAD_SIZE:
    g_value_set_int(value, rtpmoonlightpay->payload_size);
  case PROP_ADD_PADDING:
    g_value_set_boolean(value, rtpmoonlightpay->add_padding);
  case PROP_FEC_PERCENTAGE:
    g_value_set_int(value, rtpmoonlightpay->fec_percentage);
  case PROP_MIN_REQUIRED_FEC_PACKETS:
    g_value_set_int(value, rtpmoonlightpay->min_required_fec_packets);
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_rtp_moonlight_pay_dispose(GObject *object) {
  gst_rtp_moonlight_pay *rtpmoonlightpay = gst_rtp_moonlight_pay(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS(gst_rtp_moonlight_pay_parent_class)->dispose(object);
}

void gst_rtp_moonlight_pay_finalize(GObject *object) {
  gst_rtp_moonlight_pay *rtpmoonlightpay = gst_rtp_moonlight_pay(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS(gst_rtp_moonlight_pay_parent_class)->finalize(object);
}

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
 * Creates an RTP header and returns a GstBuffer to it
 */
static GstBuffer *
create_rtp_header(const gst_rtp_moonlight_pay &rtpmoonlightpay, int packet_nr, int tot_packets, GstClockTime pts) {
  constexpr auto rtp_header_size = sizeof(state::VideoRTPHeaders);
  GstBuffer *buf = gst_buffer_new_and_fill(rtp_header_size, 0x00);

  /* get WRITE access to the memory */
  GstMapInfo info;
  gst_buffer_map(buf, &info, GST_MAP_WRITE);

  /* set RTP headers */
  auto packet = (state::VideoRTPHeaders *)info.data;

  packet->rtp.packetType = 0x00;
  packet->rtp.timestamp = 0x00;
  packet->rtp.ssrc = 0x00;

  packet->packet.frameIndex = rtpmoonlightpay.frame_num;
  packet->packet.streamPacketIndex = ((uint32_t)rtpmoonlightpay.cur_seq_number + packet_nr) << 8;

  packet->packet.multiFecFlags = 0x10;
  packet->packet.multiFecBlocks = (packet_nr << 4);

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

/**
 * Split the input buffer into packets, will prepend the RTP header and append any padding if needed
 */
static GstBufferList *
generate_rtp_packets(const gst_rtp_moonlight_pay &rtpmoonlightpay, GstBuffer *inbuf, GstClockTime pts) {
  constexpr auto video_payload_header_size = 8;
  GstBuffer *video_header = gst_buffer_new_and_fill(video_payload_header_size, "\0017charss");
  inbuf = gst_buffer_append(video_header, inbuf);

  auto in_buf_size = gst_buffer_get_size(inbuf);
  auto payload_size = rtpmoonlightpay.payload_size;
  auto tot_packets = (in_buf_size / payload_size) + 1;
  GstBufferList *buffers = gst_buffer_list_new_sized(tot_packets);

  for (int packet_nr = 0; packet_nr < tot_packets; packet_nr++) {
    auto begin = packet_nr * payload_size;
    auto remaining = in_buf_size - begin;
    auto packet_payload_size = MIN(remaining, payload_size);

    GstBuffer *header = create_rtp_header(rtpmoonlightpay, packet_nr, tot_packets, pts);

    GstBuffer *payload = gst_buffer_copy_region(inbuf, GST_BUFFER_COPY_ALL, begin, packet_payload_size);
    GstBuffer *rtp_packet = gst_buffer_append(header, payload);

    if ((remaining < payload_size) && rtpmoonlightpay.add_padding) {
      GstBuffer *padding = gst_buffer_new_and_fill(payload_size - remaining, 0x00);
      gst_buffer_append(rtp_packet, padding);
    }

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
generate_fec_packets(const gst_rtp_moonlight_pay &rtpmoonlightpay, GstBufferList *rtp_packets, GstClockTime pts) {
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

    rtp_packet->rtp.header = 0x80 | FLAG_EXTENSION;
    uint16_t sequence_number = rtpmoonlightpay.cur_seq_number + shard_idx;
    rtp_packet->rtp.sequenceNumber = boost::endian::native_to_big(sequence_number);

    GstBuffer *packet_buf = gst_buffer_new_allocate(nullptr, blocksize, nullptr);
    gst_buffer_fill(packet_buf, 0, rtp_packet, blocksize);
    gst_buffer_list_insert(buffers, -1, packet_buf);
  }

  gst_buffer_unmap(rtp_payload, &info);

  return buffers;
}

/**
 * Overrides the default generate_output method so that we can turn the input buffer (ideally an encoded stream)
 * into a list of buffers: a series of RTP packets encoded following the Moonlight protocol specs.
 */
static GstFlowReturn gst_rtp_moonlight_pay_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
  gst_rtp_moonlight_pay *rtpmoonlightpay = gst_rtp_moonlight_pay(trans);
  GstBuffer *inbuf;

  /* Retrieve stashed input buffer, if the default submit_input_buffer was run. Takes ownership back from there */
  inbuf = trans->queued_buf;
  trans->queued_buf = nullptr;

  /* we can't do anything without an input */
  if (inbuf == nullptr)
    return GST_FLOW_OK;

  auto pts = GST_BUFFER_PTS(inbuf);

  GstBufferList *rtp_packets = generate_rtp_packets(*rtpmoonlightpay, inbuf, pts);
  if (rtpmoonlightpay->fec_percentage > 0) {
    rtp_packets = generate_fec_packets(*rtpmoonlightpay, rtp_packets, pts);
  }
  rtpmoonlightpay->cur_seq_number += (int)gst_buffer_list_length(rtp_packets);
  rtpmoonlightpay->frame_num++;

  /* Send the generated packets to any downstream listener */
  gst_pad_push_list(trans->srcpad, rtp_packets);

  /* Setting outbuf to NULL and returning GST_BASE_TRANSFORM_FLOW_DROPPED will signal that we finished doing business */
  outbuf = nullptr;
  return GST_BASE_TRANSFORM_FLOW_DROPPED;
}

static gboolean plugin_init(GstPlugin *plugin) {

  /* FIXME Remember to set the rank if it's an element that is meant to be autoplugged by decodebin. */
  return gst_element_register(plugin, "rtpmoonlightpay", GST_RANK_NONE, gst_TYPE_rtp_moonlight_pay);
}

/* FIXME: these are normally defined by the GStreamer build system.
   If you are creating an element to be included in gst-plugins-*,
   remove these, as they're always defined.  Otherwise, edit as
   appropriate for your external plugin package. */
#ifndef VERSION
#define VERSION "0.0.FIXME"
#endif
#ifndef PACKAGE
#define PACKAGE "FIXME_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "FIXME_package_name"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://FIXME.org/"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  rtpmoonlightpay,
                  "FIXME plugin description",
                  plugin_init,
                  VERSION,
                  "LGPL",
                  PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)

GST_ELEMENT_REGISTER_DEFINE(rtpmoonlightpay, "rtpmoonlightpay", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay)
