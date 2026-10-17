#pragma once

#include <crypto/crypto.hpp>
#include <gst/base/gstbasetransform.h>
#include <memory>
#include <openssl/crypto.h>

G_BEGIN_DECLS

#define gst_TYPE_rtp_moonlight_pay_video (gst_rtp_moonlight_pay_video_get_type())
#define gst_rtp_moonlight_pay_video(obj)                                                                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), gst_TYPE_rtp_moonlight_pay_video, gst_rtp_moonlight_pay_video))
#define gst_rtp_moonlight_pay_video_CLASS(klass)                                                                       \
  (G_TYPE_CHECK_CLASS_CAST((klass), gst_TYPE_rtp_moonlight_pay_video, gst_rtp_moonlight_pay_videoClass))
#define gst_IS_rtp_moonlight_pay_video(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), gst_TYPE_rtp_moonlight_pay_video))
#define gst_IS_rtp_moonlight_pay_video_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), gst_TYPE_rtp_moonlight_pay_video))

typedef struct _gst_rtp_moonlight_pay_video gst_rtp_moonlight_pay_video;
typedef struct _gst_rtp_moonlight_pay_videoClass gst_rtp_moonlight_pay_videoClass;

struct _gst_rtp_moonlight_pay_video {
  GstBaseTransform base_rtpmoonlightpay_video;

  int payload_size;
  bool add_padding;

  int fec_percentage;
  int min_required_fec_packets;

  u_int32_t cur_seq_number;
  u_int32_t frame_num;

  /* Video encryption (moonlight::encryption::VIDEO); see gst_moonlight_video::encrypt_rtp_packets() */
  bool encrypt;
  bool has_aes_key;
  unsigned char aes_key[16];
  guint iv_epoch;
  guint64 iv_counter;
  crypto::aes_gcm *gcm; // created from aes_key on the first encrypted frame, freed in finalize
};

struct _gst_rtp_moonlight_pay_videoClass {
  GstBaseTransformClass base_rtpmoonlightpay_video_class;
};

GType gst_rtp_moonlight_pay_video_get_type(void);

G_END_DECLS
