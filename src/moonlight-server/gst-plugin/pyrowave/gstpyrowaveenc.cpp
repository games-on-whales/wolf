// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the pyrowaveenc element. See gstpyrowaveenc.hpp for the element's contract.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst-plugin/pyrowave/gstpyrowaveenc.hpp>

#include <gst-plugin/pyrowave/pyrowave_align.hpp>
#include <gst-plugin/pyrowave/pyrowave_device.hpp>
#include <gst-plugin/utils.hpp>

#include <algorithm>
#include <drm_fourcc.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <helpers/logger.hpp>
#include <limits>
#include <moonlight/data-structures.hpp>
#include <optional>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_pyrowave_enc_debug_category);
#define GST_CAT_DEFAULT gst_pyrowave_enc_debug_category

namespace {

/**
 * Bytes the RTP layer prepends to the first payload: the Moonlight video short header.
 *
 * @see gst-plugin/video.hpp, which builds it.
 */
constexpr std::size_t VIDEO_PAYLOAD_HEADER_SIZE = 8;

/**
 * Headroom added on top of the raw bitstream size when sizing the packetizer scratch buffer, to
 * absorb the per-record headers of the packetized form.
 */
constexpr std::size_t SCRATCH_SLACK = 64 * 1024;

/** Fallback frame rate for pipelines that do not negotiate one. */
constexpr int DEFAULT_FPS = 60;

} // namespace

/**
 * Everything the element needs between caps negotiation and the next one.
 *
 * Declared in gstpyrowaveenc.hpp as an opaque type: the element struct only carries a pointer to it,
 * so the PyroWave and GStreamer headers stay out of the element's public interface.
 */
struct PyroWaveEncoderState {
  /** Shared PyroWave device; declared first so it outlives `encoder`. */
  std::shared_ptr<wolf::pyrowave::Device> device;

  /** dma-buf import cache, only present when the input is imported as a DRM dma-buf (DRM caps, or
   * legacy RGB dma-buf caps promoted to linear DRM). */
  std::optional<wolf::pyrowave::DmaBufImporter> importer;

  /** Encoder handle; must be destroyed before `device`. */
  pyrowave_encoder encoder = nullptr;

  /** Input description: DRM dma-buf caps (including legacy RGB dma-buf caps promoted to a linear DRM
   * description), or the raw video info for NV12/I420 input. */
  bool dma_drm = false;
  GstVideoInfoDmaDrm drm_info{};
  GstVideoInfo video_info{};

  /** Video info used by the CPU path; only meaningful when `cpu_fallback_possible`. */
  GstVideoInfo cpu_video_info{};

  /** Plane format for the CPU path. */
  pyrowave_cpu_buffer_format cpu_format = PYROWAVE_CPU_BUFFER_FORMAT_NV12;

  /** Encoder size in pixels; even, and possibly smaller than the input. */
  int width = 0;
  int height = 0;

  /** Input size in pixels. */
  int input_width = 0;
  int input_height = 0;

  /** Frame rate of the negotiated input. */
  int fps = DEFAULT_FPS;

  /** RTP payload size minus the RTP header, or 0 when the bitstream cannot be aligned. */
  std::size_t shard = 0;

  /**
   * Whether the CPU entry point can encode the incoming buffer.
   *
   * False when the input is an RGB dma-buf (only the GPU path can colour convert it) or when the
   * dma-buf is not linear (mapping it on the CPU would read a tiled layout as if it were packed), or
   * when the input has to be rescaled, which only the GPU path implements. The CPU entry point only
   * reads 4:2:0 YUV, so NV12 and I420 are the only formats it can take.
   */
  bool cpu_fallback_possible = false;

  /** Scratch buffers, kept across frames so that the steady state allocates nothing. */
  std::vector<pyrowave_packet> packets;
  std::vector<std::uint8_t> scratch;

  bool warned_no_framerate = false;
  bool warned_shard = false;
  bool warned_cpu_fallback = false;

  /** Destroys the encoder and drops every cached import. */
  void reset() {
    if (encoder != nullptr) {
      pyrowave_encoder_destroy(encoder);
      encoder = nullptr;
    }
    if (importer) {
      importer->clear();
      importer.reset();
    }
    device.reset();
  }

  ~PyroWaveEncoderState() {
    reset();
  }
};

/* prototypes */

static void gst_pyrowave_enc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_pyrowave_enc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_pyrowave_enc_finalize(GObject *object);

static GstCaps *
gst_pyrowave_enc_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static gboolean gst_pyrowave_enc_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_pyrowave_enc_stop(GstBaseTransform *trans);
static GstFlowReturn gst_pyrowave_enc_generate_output(GstBaseTransform *trans, GstBuffer **outbuf);

enum {
  /** Target bitrate in kbit/s. */
  PROP_BITRATE = 1,

  /** Maximum size of RTP packets. */
  PROP_PAYLOAD_SIZE = 2,

  /** Encoder width, 0 to follow the input. */
  PROP_WIDTH = 3,

  /** Encoder height, 0 to follow the input. */
  PROP_HEIGHT = 4,
};

namespace {

/* pad templates */

static GstStaticPadTemplate gst_pyrowave_enc_src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-pyrowave, "
                    "width=(int)[1,16384], "
                    "height=(int)[1,16384], "
                    "framerate=(fraction)[0/1,MAX], "
                    "chroma=(string)4:2:0"));

static GstStaticPadTemplate gst_pyrowave_enc_sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw(memory:DMABuf), "
                    "format=(string){ DMA_DRM, NV12, I420, BGRx, BGRA, RGBx, RGBA }; "
                    "video/x-raw, "
                    "format=(string){ NV12, I420 }"));

/* helper functions */

/**
 * Reads the frame size out of raw caps, accepting both DRM dma-buf caps and plain video caps.
 *
 * @param caps Caps to inspect.
 * @param width Output: frame width, untouched when the caps cannot be parsed.
 * @param height Output: frame height, untouched when the caps cannot be parsed.
 * @return true when the size could be read.
 */
gboolean caps_frame_size(GstCaps *caps, gint *width, gint *height) {
  if (caps == nullptr || !gst_caps_is_fixed(caps)) {
    return FALSE;
  }

  GstVideoInfoDmaDrm drm_info{};
  if (gst_video_info_dma_drm_from_caps(&drm_info, caps)) {
    *width = GST_VIDEO_INFO_WIDTH(&drm_info.vinfo);
    *height = GST_VIDEO_INFO_HEIGHT(&drm_info.vinfo);
    return TRUE;
  }

  GstVideoInfo info{};
  if (gst_video_info_from_caps(&info, caps)) {
    *width = GST_VIDEO_INFO_WIDTH(&info);
    *height = GST_VIDEO_INFO_HEIGHT(&info);
    return TRUE;
  }

  return FALSE;
}

/**
 * Reads the frame rate out of raw caps, accepting both DRM dma-buf caps and plain video caps.
 *
 * @param caps Caps to inspect; must be fixed.
 * @param fps_n Output: frame rate numerator.
 * @param fps_d Output: frame rate denominator.
 * @return true when the caps carry a non-zero frame rate.
 */
gboolean caps_framerate(GstCaps *caps, gint *fps_n, gint *fps_d) {
  if (caps == nullptr || !gst_caps_is_fixed(caps)) {
    return FALSE;
  }

  GstVideoInfoDmaDrm drm_info{};
  GstVideoInfo info{};
  if (gst_video_info_dma_drm_from_caps(&drm_info, caps)) {
    info = drm_info.vinfo;
  } else if (!gst_video_info_from_caps(&info, caps)) {
    return FALSE;
  }

  if (GST_VIDEO_INFO_FPS_N(&info) == 0) {
    return FALSE;
  }

  *fps_n = GST_VIDEO_INFO_FPS_N(&info);
  *fps_d = GST_VIDEO_INFO_FPS_D(&info);
  return TRUE;
}

/**
 * Computes the size the encoder runs at: the width/height properties when set, the negotiated input
 * size otherwise, rounded down to even numbers because PyroWave's 4:2:0 mode requires it.
 *
 * @param self Element holding the properties.
 * @param caps Caps of the input frame (may be nullptr when both properties are set).
 * @param width Output: encoder width.
 * @param height Output: encoder height.
 * @return true when a usable even size could be determined.
 */
gboolean encoder_size(const gst_pyrowave_enc *self, GstCaps *caps, gint *width, gint *height) {
  gint input_width = self->width;
  gint input_height = self->height;

  if ((input_width == 0 || input_height == 0) && caps != nullptr) {
    gint caps_width = 0, caps_height = 0;
    if (caps_frame_size(caps, &caps_width, &caps_height)) {
      if (input_width == 0) {
        input_width = caps_width;
      }
      if (input_height == 0) {
        input_height = caps_height;
      }
    }
  }

  *width = input_width & ~1;
  *height = input_height & ~1;
  return *width > 0 && *height > 0;
}

/**
 * Resolves the payload boundary the aligner works with: the RTP payload minus the RTP header.
 *
 * Records are laid out on 32-bit word boundaries, so a payload that is not a multiple of four bytes
 * cannot carry a padding record and alignment is turned off (0) for it.
 *
 * @param self Element holding the payload_size property.
 * @return Bytes available to the bitstream per RTP payload, or 0 to disable alignment.
 */
std::size_t compute_shard(const gst_pyrowave_enc *self) {
  const int payload_size = self->payload_size;
  if (payload_size <= static_cast<int>(MAX_RTP_HEADER_SIZE) ||
      (payload_size - static_cast<int>(MAX_RTP_HEADER_SIZE)) % 4 != 0) {
    if (!self->state->warned_shard) {
      self->state->warned_shard = true;
      GST_WARNING_OBJECT(self,
                         "payload_size=%d cannot be aligned to RTP payloads, the bitstream will not be "
                         "resynchronizable after packet loss",
                         payload_size);
    }
    return 0;
  }

  return static_cast<std::size_t>(payload_size) - MAX_RTP_HEADER_SIZE;
}

/**
 * Builds the dma-buf description of an input buffer from its video meta.
 *
 * @param state Negotiated state, for the caps-derived format and size.
 * @param inbuf Buffer to describe.
 * @return The description, or std::nullopt when the buffer is not a single-fd dma-buf.
 */
std::optional<wolf::pyrowave::DmaBufImage> dma_buf_image(const PyroWaveEncoderState &state, GstBuffer *inbuf) {
  if (gst_buffer_n_memory(inbuf) != 1) {
    // Multi-fd buffers would need one import per plane; nothing in Wolf produces those.
    return std::nullopt;
  }

  auto *memory = gst_buffer_peek_memory(inbuf, 0);
  if (memory == nullptr || !gst_is_dmabuf_memory(memory)) {
    return std::nullopt;
  }

  wolf::pyrowave::DmaBufImage image{};
  image.fd = gst_dmabuf_memory_get_fd(memory);
  image.fourcc = state.drm_info.drm_fourcc;
  image.modifier = state.drm_info.drm_modifier;
  image.width = static_cast<std::uint32_t>(state.input_width);
  image.height = static_cast<std::uint32_t>(state.input_height);

  auto *meta = gst_buffer_get_video_meta(inbuf);
  if (meta == nullptr) {
    return std::nullopt;
  }

  image.num_planes = std::min<std::size_t>(meta->n_planes, wolf::pyrowave::MAX_DMABUF_PLANES);
  for (std::size_t i = 0; i < image.num_planes; i++) {
    image.planes[i].offset = meta->offset[i];
    image.planes[i].pitch = meta->stride[i];
  }

  return image;
}

/**
 * Fills the CPU buffer description for an input frame.
 *
 * @param state Negotiated state.
 * @param frame Mapped input frame.
 * @param buffer Output: the pyrowave CPU buffer, pointing straight into the mapped frame.
 */
void fill_cpu_buffer(const PyroWaveEncoderState &state, const GstVideoFrame &frame, pyrowave_cpu_buffer *buffer) {
  *buffer = {};
  const int planes = GST_VIDEO_INFO_N_PLANES(&state.cpu_video_info);
  for (int i = 0; i < planes; i++) {
    buffer->data[i] = GST_VIDEO_FRAME_PLANE_DATA(&frame, i);
    const auto stride = static_cast<std::size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&frame, i));
    buffer->row_stride_in_bytes[i] = stride;
    // 4:2:0: the chroma planes are half the height of the luma plane.
    const int plane_height = i == 0 ? state.height : (state.height + 1) / 2;
    buffer->plane_size_in_bytes[i] = stride * static_cast<std::size_t>(plane_height);
  }

  buffer->width = state.width;
  buffer->height = state.height;
  buffer->format = state.cpu_format;
}

/**
 * Maps a raw video format to the plane layout the CPU entry point reads.
 *
 * @param format Negotiated raw video format.
 * @return The CPU buffer format, or std::nullopt when the CPU entry point cannot read the format
 * (anything but 4:2:0 YUV, e.g. the RGB dma-buf formats of the sink template).
 */
std::optional<pyrowave_cpu_buffer_format> cpu_buffer_format(GstVideoFormat format) {
  switch (format) {
  case GST_VIDEO_FORMAT_NV12:
    return PYROWAVE_CPU_BUFFER_FORMAT_NV12;
  case GST_VIDEO_FORMAT_I420:
    return PYROWAVE_CPU_BUFFER_FORMAT_YUV420P;
  default:
    return std::nullopt;
  }
}

} // namespace

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(gst_pyrowave_enc,
                        gst_pyrowave_enc,
                        GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_pyrowave_enc_debug_category,
                                                "pyrowaveenc",
                                                0,
                                                "debug category for pyrowaveenc element"));

static void gst_pyrowave_enc_class_init(gst_pyrowave_encClass *klass) {
  auto *gobject_class = G_OBJECT_CLASS(klass);
  auto *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &gst_pyrowave_enc_src_template);
  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &gst_pyrowave_enc_sink_template);

  gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
                                        "PyroWave encoder",
                                        "Codec/Encoder/Video",
                                        "Encodes raw video into a PyroWave bitstream",
                                        "Wolf <https://github.com/games-on-whales/wolf/>");

  gobject_class->set_property = gst_pyrowave_enc_set_property;
  gobject_class->get_property = gst_pyrowave_enc_get_property;
  gobject_class->finalize = gst_pyrowave_enc_finalize;

  g_object_class_install_property(
      gobject_class,
      PROP_BITRATE,
      g_param_spec_int("bitrate", "bitrate", "Target bitrate in kbit/s", 1, G_MAXINT, 15500, G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_PAYLOAD_SIZE,
                                  g_param_spec_int("payload_size",
                                                   "payload_size",
                                                   "Maximum size of RTP packets, used to align the bitstream "
                                                   "with the packet boundaries",
                                                   0,
                                                   10240,
                                                   1392,
                                                   G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_WIDTH,
                                  g_param_spec_int("width",
                                                   "width",
                                                   "Encoder width, 0 to follow the negotiated input",
                                                   0,
                                                   16384,
                                                   0,
                                                   G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
                                  PROP_HEIGHT,
                                  g_param_spec_int("height",
                                                   "height",
                                                   "Encoder height, 0 to follow the negotiated input",
                                                   0,
                                                   16384,
                                                   0,
                                                   G_PARAM_READWRITE));

  base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_pyrowave_enc_transform_caps);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_pyrowave_enc_set_caps);
  base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_pyrowave_enc_stop);
  base_transform_class->generate_output = GST_DEBUG_FUNCPTR(gst_pyrowave_enc_generate_output);
}

static void gst_pyrowave_enc_init(gst_pyrowave_enc *pyrowave_enc) {
  pyrowave_enc->bitrate = 15500;
  pyrowave_enc->payload_size = 1392;
  pyrowave_enc->width = 0;
  pyrowave_enc->height = 0;
  pyrowave_enc->state = new PyroWaveEncoderState();
}

static void gst_pyrowave_enc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  auto *pyrowave_enc = gst_pyrowave_enc(object);

  GST_DEBUG_OBJECT(pyrowave_enc, "set_property");

  switch (property_id) {
  case PROP_BITRATE:
    pyrowave_enc->bitrate = g_value_get_int(value);
    break;
  case PROP_PAYLOAD_SIZE:
    pyrowave_enc->payload_size = g_value_get_int(value);
    break;
  case PROP_WIDTH:
    pyrowave_enc->width = g_value_get_int(value);
    break;
  case PROP_HEIGHT:
    pyrowave_enc->height = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void gst_pyrowave_enc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  auto *pyrowave_enc = gst_pyrowave_enc(object);

  GST_DEBUG_OBJECT(pyrowave_enc, "get_property");

  switch (property_id) {
  case PROP_BITRATE:
    g_value_set_int(value, pyrowave_enc->bitrate);
    break;
  case PROP_PAYLOAD_SIZE:
    g_value_set_int(value, pyrowave_enc->payload_size);
    break;
  case PROP_WIDTH:
    g_value_set_int(value, pyrowave_enc->width);
    break;
  case PROP_HEIGHT:
    g_value_set_int(value, pyrowave_enc->height);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void gst_pyrowave_enc_finalize(GObject *object) {
  auto *pyrowave_enc = gst_pyrowave_enc(object);

  GST_DEBUG_OBJECT(pyrowave_enc, "finalize");

  delete pyrowave_enc->state;
  pyrowave_enc->state = nullptr;

  G_OBJECT_CLASS(gst_pyrowave_enc_parent_class)->finalize(object);
}

/**
 * Advertises the encoder's own frame size downstream, derived from the properties and the input
 * caps.
 */
static GstCaps *
gst_pyrowave_enc_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter) {
  auto *pyrowave_enc = gst_pyrowave_enc(trans);

  GstCaps *result;
  if (direction == GST_PAD_SINK) {
    // From the caps of the incoming frame to the caps of the encoded one.
    result = gst_static_pad_template_get_caps(&gst_pyrowave_enc_src_template);

    gint width = 0, height = 0;
    if (encoder_size(pyrowave_enc, caps, &width, &height)) {
      gint fps_n = DEFAULT_FPS, fps_d = 1;
      caps_framerate(caps, &fps_n, &fps_d);

      result = gst_caps_make_writable(result);
      auto *structure = gst_caps_get_structure(result, 0);
      gst_structure_set(structure,
                        "width",
                        G_TYPE_INT,
                        width,
                        "height",
                        G_TYPE_INT,
                        height,
                        "framerate",
                        GST_TYPE_FRACTION,
                        fps_n,
                        fps_d,
                        nullptr);
    }
  } else {
    result = gst_static_pad_template_get_caps(&gst_pyrowave_enc_sink_template);
  }

  if (filter != nullptr) {
    GstCaps *filtered = gst_caps_intersect_full(filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(result);
    result = filtered;
  }

  return result;
}

/**
 * Sets up (or rebuilds, on renegotiation) the encoder for the negotiated input.
 */
static gboolean gst_pyrowave_enc_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
  auto *pyrowave_enc = gst_pyrowave_enc(trans);
  auto *state = pyrowave_enc->state;

  state->reset();
  state->dma_drm = false;
  state->cpu_fallback_possible = false;
  state->video_info = {};
  state->drm_info = {};
  state->cpu_video_info = {};

  const gboolean dmabuf = gst_caps_features_contains(gst_caps_get_features(incaps, 0), GST_CAPS_FEATURE_MEMORY_DMABUF);
  const gboolean has_drm_format = gst_structure_has_field(gst_caps_get_structure(incaps, 0), "drm-format");

  if (dmabuf && has_drm_format) {
    if (!gst_video_info_dma_drm_from_caps(&state->drm_info, incaps)) {
      GST_ELEMENT_ERROR(pyrowave_enc,
                        STREAM,
                        FORMAT,
                        ("PyroWave: unusable drm-format in %" GST_PTR_FORMAT, incaps),
                        ("Set WOLF_USE_ZERO_COPY=FALSE, or fix the drm-format list"));
      return FALSE;
    }
    state->dma_drm = TRUE;
    state->video_info = state->drm_info.vinfo;
  } else {
    if (!gst_video_info_from_caps(&state->video_info, incaps)) {
      GST_ERROR_OBJECT(pyrowave_enc, "unsupported input caps %" GST_PTR_FORMAT, incaps);
      return FALSE;
    }
    if (GST_VIDEO_INFO_N_PLANES(&state->video_info) == 0) {
      // The DRM pseudo-format lands here whenever a dma-buf is described without a drm-format field.
      GST_ELEMENT_ERROR(pyrowave_enc,
                        STREAM,
                        FORMAT,
                        ("PyroWave: the input caps carry no usable video format: %" GST_PTR_FORMAT, incaps),
                        (nullptr));
      return FALSE;
    }

    const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&state->video_info);
    if (!cpu_buffer_format(format)) {
      if (!dmabuf) {
        // The sink template only accepts NV12/I420 in system memory, so this is unreachable today;
        // it guards the case of the template being widened without the CPU path following.
        GST_ELEMENT_ERROR(pyrowave_enc,
                          STREAM,
                          FORMAT,
                          ("PyroWave cannot encode %s from system memory", gst_video_format_to_string(format)),
                          ("Convert the input to NV12 or I420"));
        return FALSE;
      }
      // Dma-buf caps without a drm-format field describe a linear buffer. The CPU entry point only
      // reads 4:2:0 YUV, so describe the RGB frame as a linear DRM buffer and let the GPU importer
      // colour convert it.
      if (!gst_video_info_dma_drm_from_video_info(&state->drm_info, &state->video_info, DRM_FORMAT_MOD_LINEAR)) {
        GST_ELEMENT_ERROR(pyrowave_enc,
                          STREAM,
                          FORMAT,
                          ("PyroWave: no DRM fourcc for dma-buf format %s", gst_video_format_to_string(format)),
                          ("Set WOLF_USE_ZERO_COPY=FALSE to use the CPU pipeline"));
        return FALSE;
      }
      state->dma_drm = TRUE;
    }
  }

  state->input_width = GST_VIDEO_INFO_WIDTH(&state->video_info);
  state->input_height = GST_VIDEO_INFO_HEIGHT(&state->video_info);

  gint width = 0, height = 0;
  if (!encoder_size(pyrowave_enc, incaps, &width, &height)) {
    GST_ERROR_OBJECT(pyrowave_enc, "cannot determine the encoder size from %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }
  state->width = width;
  state->height = height;

  state->fps = DEFAULT_FPS;
  if (GST_VIDEO_INFO_FPS_N(&state->video_info) != 0) {
    state->fps = GST_VIDEO_INFO_FPS_N(&state->video_info) / GST_VIDEO_INFO_FPS_D(&state->video_info);
  }
  if (state->fps <= 0) {
    state->fps = DEFAULT_FPS;
    if (!state->warned_no_framerate) {
      state->warned_no_framerate = true;
      GST_DEBUG_OBJECT(pyrowave_enc, "no frame rate negotiated, assuming %d fps", DEFAULT_FPS);
    }
  }

  const gboolean even_size = (state->input_width & 1) == 0 && (state->input_height & 1) == 0;
  const gboolean same_size = state->input_width == state->width && state->input_height == state->height;

  // Which colour layouts the CPU entry point can take.
  if (state->dma_drm) {
    state->cpu_fallback_possible = wolf::pyrowave::drm_modifier_is_linear(state->drm_info.drm_modifier) && even_size &&
                                   same_size && wolf::pyrowave::drm_format_is_nv12(state->drm_info.drm_fourcc);
    if (state->cpu_fallback_possible) {
      // A linear NV12 dma-buf is readable from the CPU, so describe it as an ordinary NV12 frame.
      gst_video_info_set_format(&state->cpu_video_info, GST_VIDEO_FORMAT_NV12, state->width, state->height);
      state->cpu_format = PYROWAVE_CPU_BUFFER_FORMAT_NV12;
    }
  } else {
    const auto cpu_format = cpu_buffer_format(GST_VIDEO_INFO_FORMAT(&state->video_info));
    state->cpu_fallback_possible = even_size && same_size && cpu_format.has_value();
    if (state->cpu_fallback_possible) {
      state->cpu_video_info = state->video_info;
      state->cpu_format = *cpu_format;
    }
  }

  if (state->dma_drm && !wolf::pyrowave::drm_format_to_vk_format(state->drm_info.drm_fourcc)) {
    GST_ELEMENT_ERROR(pyrowave_enc,
                      STREAM,
                      FORMAT,
                      ("PyroWave cannot consume DRM format 0x%08x", state->drm_info.drm_fourcc),
                      ("Set WOLF_USE_ZERO_COPY=FALSE to use the CPU pipeline"));
    return FALSE;
  }

  if (!state->dma_drm && !state->cpu_fallback_possible) {
    GST_ELEMENT_ERROR(pyrowave_enc,
                      STREAM,
                      FORMAT,
                      ("PyroWave needs the input at the encoder resolution (%dx%d), got %dx%d",
                       state->width,
                       state->height,
                       state->input_width,
                       state->input_height),
                      ("Fix the video_params of the pyrowave encoder in config.toml, or set "
                       "WOLF_USE_ZERO_COPY=TRUE to let the GPU rescale"));
    return FALSE;
  }

  state->shard = compute_shard(pyrowave_enc);

  state->device = wolf::pyrowave::acquire_device();
  if (!state->device) {
    GST_ELEMENT_ERROR(pyrowave_enc,
                      LIBRARY,
                      INIT,
                      ("PyroWave: no usable Vulkan device"),
                      ("Set WOLF_USE_ZERO_COPY=FALSE, or install a Vulkan driver for the encoder GPU"));
    return FALSE;
  }

  if (state->dma_drm) {
    state->importer.emplace(state->device);
  }

  pyrowave_encoder_create_info create_info{};
  create_info.device = state->device->get();
  create_info.width = state->width;
  create_info.height = state->height;
  create_info.chroma = PYROWAVE_CHROMA_SUBSAMPLING_420;

  if (auto result = pyrowave_encoder_create(&create_info, &state->encoder);
      result != PYROWAVE_SUCCESS || state->encoder == nullptr) {
    state->encoder = nullptr;
    GST_ELEMENT_ERROR(
        pyrowave_enc,
        STREAM,
        ENCODE,
        ("PyroWave: cannot create an encoder for %dx%d (error %d)", state->width, state->height, (int)result),
        (nullptr));
    return FALSE;
  }

  GST_INFO_OBJECT(pyrowave_enc,
                  "PyroWave encoder ready: %dx%d at %d fps, %d kbit/s, payload_size %d",
                  state->width,
                  state->height,
                  state->fps,
                  pyrowave_enc->bitrate,
                  pyrowave_enc->payload_size);
  return TRUE;
}

static gboolean gst_pyrowave_enc_stop(GstBaseTransform *trans) {
  auto *pyrowave_enc = gst_pyrowave_enc(trans);

  pyrowave_enc->state->reset();
  return TRUE;
}

/**
 * Encodes one input buffer and hands the result to the RTP layer.
 *
 * Every frame is a full intra frame, so no buffer is ever flagged as a delta unit: the client relies
 * on that to know it can always restart from any frame.
 */
static GstFlowReturn gst_pyrowave_enc_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
  auto *pyrowave_enc = gst_pyrowave_enc(trans);
  auto *state = pyrowave_enc->state;

  GstBuffer *inbuf = trans->queued_buf;
  trans->queued_buf = nullptr;

  if (inbuf == nullptr) {
    return GST_FLOW_OK;
  }

  if (state->encoder == nullptr) {
    gst_buffer_unref(inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  pyrowave_rate_control rate_control{};
  rate_control.maximum_bitstream_size = std::max<std::size_t>(
      static_cast<std::size_t>(pyrowave_enc->bitrate) * 1000ull / (8ull * static_cast<std::size_t>(state->fps)),
      16 * 1024);

  pyrowave_result result = PYROWAVE_ERROR_GENERIC;
  gboolean encoded = FALSE;

  if (state->dma_drm) {
    auto image = dma_buf_image(*state, inbuf);
    if (image) {
      auto imported = state->importer->import_dma_buf(*image);
      if (imported) {
        // Hand the compositor's buffer over from the foreign queue family for the duration of the
        // encode, and give it back afterwards so the next frame can acquire it again.
        pyrowave_gpu_external_reference reference{};
        reference.image = imported->image;
        reference.queue_family_index = VK_QUEUE_FAMILY_FOREIGN_EXT;

        pyrowave_gpu_sync_operation sync{};
        sync.images = &reference;
        sync.num_images = 1;
        sync.sync = {VK_NULL_HANDLE, 0};

        pyrowave_scaled_encode_info scaling{};
        scaling.view = imported->view;
        scaling.input_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        scaling.output_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        scaling.intermediate_plane_format = VK_FORMAT_R8_UNORM;
        // Limited range, BT.709 at 8 bits: what Aurora requests and decodes, and what Wolf's other
        // codecs produce. The chroma midpoint matches the H.273 8-bit value of 128.
        scaling.ycbcr_range = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        scaling.ycbcr_range_bit_depth = 8;
        scaling.ycbcr_chroma_midpoint = 128.0f / 255.0f;
        scaling.force_linear_filtering = false;
        scaling.skip_dither = false;
        scaling.crop_rect = nullptr;

        result = pyrowave_encoder_encode_gpu_scaled(state->encoder, &sync, &sync, &scaling, &rate_control);
        encoded = TRUE;

        if (result != PYROWAVE_SUCCESS) {
          GST_ELEMENT_ERROR(pyrowave_enc,
                            STREAM,
                            ENCODE,
                            ("PyroWave: GPU encode failed (error %d)", (int)result),
                            (nullptr));
          gst_buffer_unref(inbuf);
          return GST_FLOW_ERROR;
        }
      }
    }

    if (!encoded) {
      if (!state->cpu_fallback_possible) {
        GST_ELEMENT_ERROR(pyrowave_enc,
                          STREAM,
                          FORMAT,
                          ("PyroWave: cannot import the dma-buf nor encode it on the CPU"),
                          ("Set WOLF_USE_ZERO_COPY=FALSE to use the CPU pipeline"));
        gst_buffer_unref(inbuf);
        return GST_FLOW_ERROR;
      }
      if (!state->warned_cpu_fallback) {
        state->warned_cpu_fallback = true;
        GST_WARNING_OBJECT(pyrowave_enc, "PyroWave: falling back to the CPU encoder");
      }
    }
  }

  if (!encoded) {
    GstVideoFrame frame;
    if (!gst_video_frame_map(&frame, &state->cpu_video_info, inbuf, GST_MAP_READ)) {
      GST_ELEMENT_ERROR(pyrowave_enc, STREAM, FORMAT, ("PyroWave: cannot map the input buffer"), (nullptr));
      gst_buffer_unref(inbuf);
      return GST_FLOW_ERROR;
    }

    pyrowave_cpu_buffer cpu_buffer{};
    fill_cpu_buffer(*state, frame, &cpu_buffer);
    result = pyrowave_encoder_encode_cpu(state->encoder, &cpu_buffer, &rate_control);
    gst_video_frame_unmap(&frame);

    if (result != PYROWAVE_SUCCESS) {
      GST_ELEMENT_ERROR(pyrowave_enc,
                        STREAM,
                        ENCODE,
                        ("PyroWave: CPU encode failed (error %d)", (int)result),
                        (nullptr));
      gst_buffer_unref(inbuf);
      return GST_FLOW_ERROR;
    }
  }

  // Size the scratch buffer from what the GPU actually produced rather than from the rate control
  // target, which the encoder is not obliged to respect.
  const void *mapped_bitstream = nullptr;
  const void *mapped_metadata = nullptr;
  std::size_t mapped_bitstream_size = 0, mapped_metadata_size = 0;
  result = pyrowave_encoder_get_mapped_raw_bitstream(state->encoder,
                                                     &mapped_bitstream,
                                                     &mapped_bitstream_size,
                                                     &mapped_metadata,
                                                     &mapped_metadata_size);
  if (result != PYROWAVE_SUCCESS) {
    GST_ELEMENT_ERROR(pyrowave_enc,
                      STREAM,
                      ENCODE,
                      ("PyroWave: cannot map the raw bitstream"),
                      ("error %d", (int)result));
    gst_buffer_unref(inbuf);
    return GST_FLOW_ERROR;
  }

  const std::size_t scratch_size = mapped_bitstream_size + SCRATCH_SLACK;
  if (state->scratch.size() < scratch_size) {
    state->scratch.resize(scratch_size);
  }

  // With alignment off the whole bitstream goes into a single packet, which the RTP layer then
  // splits wherever it pleases.
  const std::size_t boundary = state->shard == 0 ? std::numeric_limits<std::size_t>::max() : state->shard;
  std::size_t num_packets = 0;
  result = pyrowave_encoder_compute_num_packets_with_padding(state->encoder,
                                                             boundary,
                                                             VIDEO_PAYLOAD_HEADER_SIZE,
                                                             &num_packets);
  if (result != PYROWAVE_SUCCESS) {
    GST_ELEMENT_ERROR(pyrowave_enc, STREAM, ENCODE, ("PyroWave: cannot size the bitstream"), ("error %d", (int)result));
    gst_buffer_unref(inbuf);
    return GST_FLOW_ERROR;
  }

  if (state->packets.size() < num_packets) {
    state->packets.resize(num_packets);
  }

  std::size_t out_packets = 0;
  result = pyrowave_encoder_packetize_with_padding(state->encoder,
                                                   state->packets.data(),
                                                   boundary,
                                                   VIDEO_PAYLOAD_HEADER_SIZE,
                                                   &out_packets,
                                                   state->scratch.data(),
                                                   state->scratch.size());
  if (result != PYROWAVE_SUCCESS || out_packets > state->packets.size()) {
    GST_ELEMENT_ERROR(pyrowave_enc,
                      STREAM,
                      ENCODE,
                      ("PyroWave: cannot packetize the bitstream"),
                      ("error %d, %zu packets", (int)result, out_packets));
    gst_buffer_unref(inbuf);
    return GST_FLOW_ERROR;
  }

  const std::size_t total_size =
      wolf::gst_pyrowave::aligned_size(state->packets.data(), out_packets, state->shard, VIDEO_PAYLOAD_HEADER_SIZE);

  GstBuffer *out = gst_buffer_new_allocate(nullptr, total_size, nullptr);
  if (out == nullptr) {
    GST_ELEMENT_ERROR(pyrowave_enc,
                      RESOURCE,
                      NO_SPACE_LEFT,
                      ("PyroWave: cannot allocate the output buffer"),
                      (nullptr));
    gst_buffer_unref(inbuf);
    return GST_FLOW_ERROR;
  }

  if (total_size > 0) {
    GstMapInfo map;
    if (!gst_buffer_map(out, &map, GST_MAP_WRITE)) {
      GST_ELEMENT_ERROR(pyrowave_enc, RESOURCE, WRITE, ("PyroWave: cannot map the output buffer"), (nullptr));
      gst_buffer_unref(out);
      gst_buffer_unref(inbuf);
      return GST_FLOW_ERROR;
    }

    const auto written = wolf::gst_pyrowave::write_aligned(map.data,
                                                           map.size,
                                                           state->scratch.data(),
                                                           state->packets.data(),
                                                           out_packets,
                                                           state->shard,
                                                           VIDEO_PAYLOAD_HEADER_SIZE);
    gst_buffer_unmap(out, &map);

    if (written != total_size) {
      GST_ELEMENT_ERROR(pyrowave_enc,
                        STREAM,
                        ENCODE,
                        ("PyroWave: aligned %zu bytes, expected %zu", written, total_size),
                        (nullptr));
      gst_buffer_unref(out);
      gst_buffer_unref(inbuf);
      return GST_FLOW_ERROR;
    }
  }

  gst_copy_timestamps(inbuf, out);
  gst_buffer_unref(inbuf);

  *outbuf = out;
  return GST_FLOW_OK;
}
