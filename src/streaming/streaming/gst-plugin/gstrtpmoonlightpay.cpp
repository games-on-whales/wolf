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
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

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
  PROP_MIN_REQUIRED_FEC_PACKETS = 22,

  /**
   * The type of stream (video/audio) will change the headers and content of packets
   */
  PROP_STREAM_TYPE = 23
};

GType gst_stream_type_get_type(void) {
  static GType gst_stream_type = 0;

  static const GEnumValue mpeg2enc_formats[] = {
      {VIDEO, "Video format", "0"}, //
      {AUDIO, "Audio format", "1"}  //
  };
  gst_stream_type = g_enum_register_static("GstMpeg2encFormat", mpeg2enc_formats);

  return gst_stream_type;
}

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

  g_object_class_install_property(
      gobject_class,
      PROP_STREAM_TYPE,
      g_param_spec_enum("stream_type",
                        "stream_type",
                        "The type of stream (video/audio) will change the headers and content of packets",
                        gst_stream_type_get_type(),
                        VIDEO,
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

  rtpmoonlightpay->stream_type = VIDEO;
}

void gst_rtp_moonlight_pay_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  gst_rtp_moonlight_pay *rtpmoonlightpay = gst_rtp_moonlight_pay(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay, "set_property");

  switch (property_id) {
  case PROP_PAYLOAD_SIZE:
    rtpmoonlightpay->payload_size = g_value_get_int(value);
    break;
  case PROP_ADD_PADDING:
    rtpmoonlightpay->add_padding = g_value_get_boolean(value);
    break;
  case PROP_FEC_PERCENTAGE:
    rtpmoonlightpay->fec_percentage = g_value_get_int(value);
    break;
  case PROP_MIN_REQUIRED_FEC_PACKETS:
    rtpmoonlightpay->min_required_fec_packets = g_value_get_int(value);
    break;
  case PROP_STREAM_TYPE:
    rtpmoonlightpay->stream_type = static_cast<STREAM_TYPE>(g_value_get_enum(value));
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
    break;
  case PROP_ADD_PADDING:
    g_value_set_boolean(value, rtpmoonlightpay->add_padding);
    break;
  case PROP_FEC_PERCENTAGE:
    g_value_set_int(value, rtpmoonlightpay->fec_percentage);
    break;
  case PROP_MIN_REQUIRED_FEC_PACKETS:
    g_value_set_int(value, rtpmoonlightpay->min_required_fec_packets);
    break;
  case PROP_STREAM_TYPE:
    g_value_set_enum(value, rtpmoonlightpay->stream_type);
    break;
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

  auto rtp_packets = split_into_rtp(rtpmoonlightpay, inbuf);

  /* Send the generated packets to any downstream listener */
  gst_pad_push_list(trans->srcpad, rtp_packets);

  /* Setting outbuf to NULL and returning GST_BASE_TRANSFORM_FLOW_DROPPED will signal that we finished doing business */
  outbuf = nullptr;
  return GST_BASE_TRANSFORM_FLOW_DROPPED;
}

static gboolean plugin_init(GstPlugin *plugin) {
  return gst_element_register(plugin, "rtpmoonlightpay", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay);
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