#ifndef _gst_rtp_moonlight_pay_H_
#define _gst_rtp_moonlight_pay_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define gst_TYPE_rtp_moonlight_pay (gst_rtp_moonlight_pay_get_type())
#define gst_rtp_moonlight_pay(obj)                                                                                     \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), gst_TYPE_rtp_moonlight_pay, gst_rtp_moonlight_pay))
#define gst_rtp_moonlight_pay_CLASS(klass)                                                                             \
  (G_TYPE_CHECK_CLASS_CAST((klass), gst_TYPE_rtp_moonlight_pay, gst_rtp_moonlight_payClass))
#define gst_IS_rtp_moonlight_pay(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), gst_TYPE_rtp_moonlight_pay))
#define gst_IS_rtp_moonlight_pay_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), gst_TYPE_rtp_moonlight_pay))

typedef struct _gst_rtp_moonlight_pay gst_rtp_moonlight_pay;
typedef struct _gst_rtp_moonlight_payClass gst_rtp_moonlight_payClass;

struct _gst_rtp_moonlight_pay {
  GstBaseTransform base_rtpmoonlightpay;

  int payload_size;
  bool add_padding;

  int fec_percentage;
  int min_required_fec_packets;

  int cur_seq_number;
  uint32_t frame_num;
};

struct _gst_rtp_moonlight_payClass {
  GstBaseTransformClass base_rtpmoonlightpay_class;
};

GType gst_rtp_moonlight_pay_get_type(void);

G_END_DECLS

#endif
