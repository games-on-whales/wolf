/**
 * SECTION:element-gstrtpmoonlightpay_audio
 *
 * The rtpmoonlightpay_audio element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v fakesrc ! rtpmoonlightpay_audio ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <crypto/crypto.hpp>
#include <gst-plugin/audio.hpp>
#include <gst-plugin/gstrtpmoonlightpay_audio.hpp>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC(gst_rtp_moonlight_pay_audio_debug_category);
#define GST_CAT_DEFAULT gst_rtp_moonlight_pay_audio_debug_category

/* prototypes */

static void
gst_rtp_moonlight_pay_audio_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void
gst_rtp_moonlight_pay_audio_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_rtp_moonlight_pay_audio_dispose(GObject *object);
static void gst_rtp_moonlight_pay_audio_finalize(GObject *object);

static GstFlowReturn gst_rtp_moonlight_pay_audio_generate_output(GstBaseTransform *trans, GstBuffer **outbuf);

enum {

  /**
   * Set to TRUE in order to enable encryption, aes_key and aes_iv must be set tooma
   */
  PROP_AES_ENCRYPTION = 19,

  /**
   * The base64 encoded AES key used to encrypt packages
   */
  PROP_AES_KEY,

  /**
   * The base64 encoded AES IV used to encrypt packages
   */
  PROP_AES_IV,

  /**
   * The duration (in ms) of the audio payload
   */
  PROP_PACKET_DURATION
};

/* pad templates */

static GstStaticPadTemplate gst_rtp_moonlight_pay_audio_src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

static GstStaticPadTemplate gst_rtp_moonlight_pay_audio_sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(gst_rtp_moonlight_pay_audio,
                        gst_rtp_moonlight_pay_audio,
                        GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_rtp_moonlight_pay_audio_debug_category,
                                                "rtpmoonlightpay_audio",
                                                0,
                                                "debug category for rtpmoonlightpay_audio element"));

static void gst_rtp_moonlight_pay_audio_class_init(gst_rtp_moonlight_pay_audioClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &gst_rtp_moonlight_pay_audio_src_template);
  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &gst_rtp_moonlight_pay_audio_sink_template);

  gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
                                        "FIXME Long name",
                                        "Generic",
                                        "FIXME Description",
                                        "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_rtp_moonlight_pay_audio_set_property;
  gobject_class->get_property = gst_rtp_moonlight_pay_audio_get_property;

  g_object_class_install_property(
      gobject_class,
      PROP_AES_ENCRYPTION,
      g_param_spec_boolean("encrypt",
                           "encrypt",
                           "Set to TRUE in order to enable encryption, aes_key and aes_iv must be set tooma",
                           TRUE,
                           G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_AES_KEY,
                                  g_param_spec_string("aes_key",
                                                      "aes_key",
                                                      "The base64 encoded AES key used to encrypt packages",
                                                      nullptr,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_AES_IV,
                                  g_param_spec_string("aes_iv",
                                                      "aes_iv",
                                                      "The base64 encoded AES IV used to encrypt packages",
                                                      nullptr,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_PACKET_DURATION,
                                  g_param_spec_int("packet_duration",
                                                   "packet_duration",
                                                   "The duration (in ms) of the audio payload",
                                                   0,
                                                   60,
                                                   5,
                                                   G_PARAM_READWRITE));

  gobject_class->dispose = gst_rtp_moonlight_pay_audio_dispose;
  gobject_class->finalize = gst_rtp_moonlight_pay_audio_finalize;

  base_transform_class->generate_output = GST_DEBUG_FUNCPTR(gst_rtp_moonlight_pay_audio_generate_output);
}

static void gst_rtp_moonlight_pay_audio_init(gst_rtp_moonlight_pay_audio *rtpmoonlightpay_audio) {
  rtpmoonlightpay_audio->cur_seq_number = 0;

  rtpmoonlightpay_audio->encrypt = true;

  rtpmoonlightpay_audio->packet_duration = 5;
  rtpmoonlightpay_audio->packets_buffer = new unsigned char *[AUDIO_TOTAL_SHARDS];
  for (int i = 0; i < AUDIO_TOTAL_SHARDS; i++) {
    rtpmoonlightpay_audio->packets_buffer[i] = new unsigned char[AUDIO_MAX_BLOCK_SIZE];
  }

  auto rs = moonlight::fec::create(AUDIO_DATA_SHARDS, AUDIO_FEC_SHARDS);
  memcpy(rs->p, AUDIO_FEC_PARITY, sizeof(AUDIO_FEC_PARITY));
  rtpmoonlightpay_audio->rs = std::move(rs);
}

void gst_rtp_moonlight_pay_audio_set_property(GObject *object,
                                              guint property_id,
                                              const GValue *value,
                                              GParamSpec *pspec) {
  gst_rtp_moonlight_pay_audio *rtpmoonlightpay_audio = gst_rtp_moonlight_pay_audio(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay_audio, "set_property");

  switch (property_id) {
  case PROP_AES_ENCRYPTION:
    rtpmoonlightpay_audio->encrypt = g_value_get_boolean(value);
    break;
  case PROP_AES_KEY:
    rtpmoonlightpay_audio->aes_key = crypto::hex_to_str(g_value_get_string(value), true);
    break;
  case PROP_AES_IV:
    rtpmoonlightpay_audio->aes_iv = g_value_get_string(value);
    break;
  case PROP_PACKET_DURATION:
    rtpmoonlightpay_audio->packet_duration = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_rtp_moonlight_pay_audio_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  gst_rtp_moonlight_pay_audio *rtpmoonlightpay_audio = gst_rtp_moonlight_pay_audio(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay_audio, "get_property");

  switch (property_id) {
  case PROP_AES_ENCRYPTION:
    g_value_set_boolean(value, rtpmoonlightpay_audio->encrypt);
    break;
  case PROP_AES_KEY:
    g_value_set_string(value, rtpmoonlightpay_audio->aes_key.c_str());
    break;
  case PROP_AES_IV:
    g_value_set_string(value, rtpmoonlightpay_audio->aes_iv.c_str());
    break;
  case PROP_PACKET_DURATION:
    g_value_set_int(value, rtpmoonlightpay_audio->packet_duration);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_rtp_moonlight_pay_audio_dispose(GObject *object) {
  gst_rtp_moonlight_pay_audio *rtpmoonlightpay_audio = gst_rtp_moonlight_pay_audio(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay_audio, "dispose");

  // free rtpmoonlightpay_audio->packets_buffer matrix
  for (int i = 0; i < AUDIO_TOTAL_SHARDS; i++) {
    delete[] rtpmoonlightpay_audio->packets_buffer[i];
  }
  delete[] rtpmoonlightpay_audio->packets_buffer;

  G_OBJECT_CLASS(gst_rtp_moonlight_pay_audio_parent_class)->dispose(object);
}

void gst_rtp_moonlight_pay_audio_finalize(GObject *object) {
  gst_rtp_moonlight_pay_audio *rtpmoonlightpay_audio = gst_rtp_moonlight_pay_audio(object);

  GST_DEBUG_OBJECT(rtpmoonlightpay_audio, "finalize");

  G_OBJECT_CLASS(gst_rtp_moonlight_pay_audio_parent_class)->finalize(object);
}

/**
 * Overrides the default generate_output method so that we can turn the input buffer (ideally an encoded stream)
 * into a list of buffers: a series of RTP packets encoded following the Moonlight protocol specs.
 */
static GstFlowReturn gst_rtp_moonlight_pay_audio_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
  gst_rtp_moonlight_pay_audio *rtpmoonlightpay_audio = gst_rtp_moonlight_pay_audio(trans);
  GstBuffer *inbuf;

  /* Retrieve stashed input buffer, if the default submit_input_buffer was run. Takes ownership back from there */
  inbuf = trans->queued_buf;
  trans->queued_buf = nullptr;

  /* we can't do anything without an input */
  if (inbuf == nullptr)
    return GST_FLOW_OK;

  auto rtp_packets = audio::split_into_rtp(rtpmoonlightpay_audio, inbuf);

  /* Send the generated packets to any downstream listener */
  gst_pad_push_list(trans->srcpad, rtp_packets);

  gst_buffer_unref(inbuf);

  /* Setting outbuf to NULL and returning GST_BASE_TRANSFORM_FLOW_DROPPED will signal that we finished doing business */
  outbuf = nullptr;
  return GST_BASE_TRANSFORM_FLOW_DROPPED;
}

static gboolean plugin_init(GstPlugin *plugin) {
  return gst_element_register(plugin, "rtpmoonlightpay_audio", GST_RANK_PRIMARY, gst_TYPE_rtp_moonlight_pay_audio);
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
                  rtpmoonlightpay_audio,
                  "FIXME plugin description",
                  plugin_init,
                  VERSION,
                  "LGPL",
                  PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)