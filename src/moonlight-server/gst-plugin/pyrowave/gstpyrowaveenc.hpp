// SPDX-License-Identifier: GPL-3.0-or-later
//
// PyroWave encoder element.
//
// pyrowaveenc turns raw video into a PyroWave bitstream: it either imports the incoming dma-buf
// straight into Vulkan (colouring and scaling on the GPU) or falls back to the CPU entry point for
// system memory. The bitstream records are laid out so that every record that fits inside one RTP
// payload starts at a payload boundary, which is what lets the client's depacketizer resynchronise
// after a lost packet.
//
// The element emits one buffer per input buffer and never marks a buffer as a delta unit: every
// PyroWave frame is a full intra frame.

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define gst_TYPE_pyrowave_enc (gst_pyrowave_enc_get_type())
#define gst_pyrowave_enc(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), gst_TYPE_pyrowave_enc, gst_pyrowave_enc))
#define gst_pyrowave_enc_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), gst_TYPE_pyrowave_enc, gst_pyrowave_encClass))
#define gst_IS_pyrowave_enc(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), gst_TYPE_pyrowave_enc))
#define gst_IS_pyrowave_enc_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), gst_TYPE_pyrowave_enc))

typedef struct _gst_pyrowave_enc gst_pyrowave_enc;
typedef struct _gst_pyrowave_encClass gst_pyrowave_encClass;

/** Opaque encoder state: device, encoder, scratch buffers. Defined in the implementation. */
struct PyroWaveEncoderState;

struct _gst_pyrowave_enc {
  GstBaseTransform base_pyrowave_enc;

  /** Target bitrate in kbit/s, handed to the pipeline as {bitrate}. */
  int bitrate;

  /** RTP packet size in bytes, handed to the pipeline as {payload_size}. */
  int payload_size;

  /** Encoder width; 0 means "use the negotiated input width". */
  int width;

  /** Encoder height; 0 means "use the negotiated input height". */
  int height;

  PyroWaveEncoderState *state;
};

struct _gst_pyrowave_encClass {
  GstBaseTransformClass base_pyrowave_enc_class;
};

/** @return The GType of the pyrowaveenc element. */
GType gst_pyrowave_enc_get_type(void);

G_END_DECLS
