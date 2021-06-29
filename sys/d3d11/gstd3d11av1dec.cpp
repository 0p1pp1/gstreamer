/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-d3d11av19dec
 * @title: d3d11av1dec
 *
 * A Direct3D11/DXVA based AV1 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/av1/file ! parsebin ! d3d11av1dec ! d3d11videosink
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11av1dec.h"

#include <gst/codecs/gstav1decoder.h>
#include <string.h>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

/* Might not be defined in dxva.h, copied from DXVA AV1 spec available at
 * https://www.microsoft.com/en-us/download/confirmation.aspx?id=101577
 * and modified with "GST_" prefix
 */
#pragma pack(push, 1)
typedef struct _GST_DXVA_PicEntry_AV1
{
  UINT width;
  UINT height;

  // Global motion parameters
  INT wmmat[6];
  union
  {
    struct
    {
      UCHAR wminvalid:1;
      UCHAR wmtype:2;
      UCHAR Reserved:5;
    };
    UCHAR wGlobalMotionFlags;
  };

  UCHAR Index;
  USHORT Reserved16Bits;

} GST_DXVA_PicEntry_AV1;

/* AV1 picture parameters structure */
typedef struct _GST_DXVA_PicParams_AV1
{
  UINT width;
  UINT height;

  UINT max_width;
  UINT max_height;

  UCHAR CurrPicTextureIndex;
  UCHAR superres_denom;
  UCHAR bitdepth;
  UCHAR seq_profile;

  // Tiles:
  struct
  {
    UCHAR cols;
    UCHAR rows;
    USHORT context_update_id;
    USHORT widths[64];
    USHORT heights[64];
  } tiles;

  // Coding Tools
  union
  {
    struct
    {
      UINT use_128x128_superblock:1;
      UINT intra_edge_filter:1;
      UINT interintra_compound:1;
      UINT masked_compound:1;
      UINT warped_motion:1;
      UINT dual_filter:1;
      UINT jnt_comp:1;
      UINT screen_content_tools:1;
      UINT integer_mv:1;
      UINT cdef:1;
      UINT restoration:1;
      UINT film_grain:1;
      UINT intrabc:1;
      UINT high_precision_mv:1;
      UINT switchable_motion_mode:1;
      UINT filter_intra:1;
      UINT disable_frame_end_update_cdf:1;
      UINT disable_cdf_update:1;
      UINT reference_mode:1;
      UINT skip_mode:1;
      UINT reduced_tx_set:1;
      UINT superres:1;
      UINT tx_mode:2;
      UINT use_ref_frame_mvs:1;
      UINT enable_ref_frame_mvs:1;
      UINT reference_frame_update:1;
      UINT Reserved:5;
    };
    UINT32 CodingParamToolFlags;
  } coding;

  // Format & Picture Info flags
  union
  {
    struct
    {
      UCHAR frame_type:2;
      UCHAR show_frame:1;
      UCHAR showable_frame:1;
      UCHAR subsampling_x:1;
      UCHAR subsampling_y:1;
      UCHAR mono_chrome:1;
      UCHAR Reserved:1;
    };
    UCHAR FormatAndPictureInfoFlags;
  } format;

  // References
  UCHAR primary_ref_frame;
  UCHAR order_hint;
  UCHAR order_hint_bits;

  GST_DXVA_PicEntry_AV1 frame_refs[7];
  UCHAR RefFrameMapTextureIndex[8];

  // Loop filter parameters
  struct
  {
    UCHAR filter_level[2];
    UCHAR filter_level_u;
    UCHAR filter_level_v;

    UCHAR sharpness_level;
    union
    {
      struct
      {
        UCHAR mode_ref_delta_enabled:1;
        UCHAR mode_ref_delta_update:1;
        UCHAR delta_lf_multi:1;
        UCHAR delta_lf_present:1;
        UCHAR Reserved:4;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;
    CHAR ref_deltas[8];
    CHAR mode_deltas[2];
    UCHAR delta_lf_res;
    UCHAR frame_restoration_type[3];
    USHORT log2_restoration_unit_size[3];
    UINT16 Reserved16Bits;
  } loop_filter;

  // Quantization
  struct
  {
    union
    {
      struct
      {
        UCHAR delta_q_present:1;
        UCHAR delta_q_res:2;
        UCHAR Reserved:5;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;

    UCHAR base_qindex;
    CHAR y_dc_delta_q;
    CHAR u_dc_delta_q;
    CHAR v_dc_delta_q;
    CHAR u_ac_delta_q;
    CHAR v_ac_delta_q;
    // using_qmatrix:
    UCHAR qm_y;
    UCHAR qm_u;
    UCHAR qm_v;
    UINT16 Reserved16Bits;
  } quantization;

  // Cdef parameters
  struct
  {
    union
    {
      struct
      {
        UCHAR damping:2;
        UCHAR bits:2;
        UCHAR Reserved:4;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;

    union
    {
      struct
      {
        UCHAR primary:6;
        UCHAR secondary:2;
      };
      UCHAR combined;
    } y_strengths[8];

    union
    {
      struct
      {
        UCHAR primary:6;
        UCHAR secondary:2;
      };
      UCHAR combined;
    } uv_strengths[8];

  } cdef;

  UCHAR interp_filter;

  // Segmentation
  struct
  {
    union
    {
      struct
      {
        UCHAR enabled:1;
        UCHAR update_map:1;
        UCHAR update_data:1;
        UCHAR temporal_update:1;
        UCHAR Reserved:4;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;
    UCHAR Reserved24Bits[3];

    union
    {
      struct
      {
        UCHAR alt_q:1;
        UCHAR alt_lf_y_v:1;
        UCHAR alt_lf_y_h:1;
        UCHAR alt_lf_u:1;
        UCHAR alt_lf_v:1;
        UCHAR ref_frame:1;
        UCHAR skip:1;
        UCHAR globalmv:1;
      };
      UCHAR mask;
    } feature_mask[8];

    SHORT feature_data[8][8];

  } segmentation;

  struct
  {
    union
    {
      struct
      {
        USHORT apply_grain:1;
        USHORT scaling_shift_minus8:2;
        USHORT chroma_scaling_from_luma:1;
        USHORT ar_coeff_lag:2;
        USHORT ar_coeff_shift_minus6:2;
        USHORT grain_scale_shift:2;
        USHORT overlap_flag:1;
        USHORT clip_to_restricted_range:1;
        USHORT matrix_coeff_is_identity:1;
        USHORT Reserved:3;
      };
      USHORT ControlFlags;
    } DUMMYUNIONNAME;

    USHORT grain_seed;
    UCHAR scaling_points_y[14][2];
    UCHAR num_y_points;
    UCHAR scaling_points_cb[10][2];
    UCHAR num_cb_points;
    UCHAR scaling_points_cr[10][2];
    UCHAR num_cr_points;
    UCHAR ar_coeffs_y[24];
    UCHAR ar_coeffs_cb[25];
    UCHAR ar_coeffs_cr[25];
    UCHAR cb_mult;
    UCHAR cb_luma_mult;
    UCHAR cr_mult;
    UCHAR cr_luma_mult;
    UCHAR Reserved8Bits;
    SHORT cb_offset;
    SHORT cr_offset;
  } film_grain;

  UINT Reserved32Bits;
  UINT StatusReportFeedbackNumber;
} GST_DXVA_PicParams_AV1;

typedef struct _GST_DXVA_Tile_AV1
{
  UINT DataOffset;
  UINT DataSize;
  USHORT row;
  USHORT column;
  USHORT Reserved16Bits;
  UCHAR anchor_frame;
  UCHAR Reserved8Bits;
} GST_DXVA_Tile_AV1;

#pragma pack(pop)

/* *INDENT-OFF* */

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_av1_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_av1_dec_debug

G_END_DECLS
/* *INDENT-ON* */

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

/* reference list 8 + 4 margin */
#define NUM_OUTPUT_VIEW 12

typedef struct _GstD3D11AV1Dec
{
  GstAV1Decoder parent;

  GstD3D11Device *device;
  GstD3D11Decoder *d3d11_decoder;

  GstAV1SequenceHeaderOBU seq_hdr;

  GST_DXVA_PicParams_AV1 pic_params;
  /* Array of GST_DXVA_Tile_AV1 */
  GArray *tile_list;

  guint written_buffer_size;
  guint remaining_buffer_size;
  guint8 *bitstream_buffer_data;

  guint max_width;
  guint max_height;
  guint bitdepth;
} GstD3D11AV1Dec;

typedef struct _GstD3D11AV1DecClass
{
  GstAV1DecoderClass parent_class;
  guint adapter;
  guint device_id;
  guint vendor_id;
} GstD3D11AV1DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_AV1_DEC(object) ((GstD3D11AV1Dec *) (object))
#define GST_D3D11_AV1_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11AV1DecClass))

static void gst_d3d11_av1_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_av1_dec_dispose (GObject * object);

static void gst_d3d11_av1_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_av1_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_av1_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_av1_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_av1_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_av1_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_av1_dec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstAV1Decoder */
static gboolean gst_d3d11_av1_dec_new_sequence (GstAV1Decoder * decoder,
    const GstAV1SequenceHeaderOBU * seq_hdr);
static gboolean gst_d3d11_av1_dec_new_picture (GstAV1Decoder * decoder,
    GstVideoCodecFrame * frame, GstAV1Picture * picture);
static GstAV1Picture *gst_d3d11_av1_dec_duplicate_picture (GstAV1Decoder *
    decoder, GstAV1Picture * picture);
static gboolean gst_d3d11_av1_dec_start_picture (GstAV1Decoder * decoder,
    GstAV1Picture * picture, GstAV1Dpb * dpb);
static gboolean gst_d3d11_av1_dec_decode_tile (GstAV1Decoder * decoder,
    GstAV1Picture * picture, GstAV1Tile * tile);
static gboolean gst_d3d11_av1_dec_end_picture (GstAV1Decoder * decoder,
    GstAV1Picture * picture);
static GstFlowReturn gst_d3d11_av1_dec_output_picture (GstAV1Decoder *
    decoder, GstVideoCodecFrame * frame, GstAV1Picture * picture);

static void
gst_d3d11_av1_dec_class_init (GstD3D11AV1DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstAV1DecoderClass *av1decoder_class = GST_AV1_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;
  gchar *long_name;

  gobject_class->get_property = gst_d3d11_av1_dec_get_property;
  gobject_class->dispose = gst_d3d11_av1_dec_dispose;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, cdata->adapter,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);

  klass->adapter = cdata->adapter;
  klass->device_id = cdata->device_id;
  klass->vendor_id = cdata->vendor_id;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_set_context);

  long_name =
      g_strdup_printf ("Direct3D11/DXVA AV1 %s Decoder", cdata->description);
  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware", "A Direct3D11/DXVA AV1 video decoder",
      "Seungha Yang <seungha@centricular.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));
  gst_d3d11_decoder_class_data_free (cdata);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_src_query);
  decoder_class->sink_event = GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_sink_event);

  av1decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_new_sequence);
  av1decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_new_picture);
  av1decoder_class->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_duplicate_picture);
  av1decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_start_picture);
  av1decoder_class->decode_tile =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_decode_tile);
  av1decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_end_picture);
  av1decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_av1_dec_output_picture);
}

static void
gst_d3d11_av1_dec_init (GstD3D11AV1Dec * self)
{
  self->tile_list = g_array_new (FALSE, TRUE, sizeof (GST_DXVA_Tile_AV1));
}

static void
gst_d3d11_av1_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11AV1DecClass *klass = GST_D3D11_AV1_DEC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_uint (value, klass->adapter);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, klass->device_id);
      break;
    case PROP_VENDOR_ID:
      g_value_set_uint (value, klass->vendor_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_av1_dec_dispose (GObject * object)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (object);

  g_clear_pointer (&self->tile_list, g_array_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_av1_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (element);
  GstD3D11AV1DecClass *klass = GST_D3D11_AV1_DEC_GET_CLASS (self);

  gst_d3d11_handle_set_context (element, context, klass->adapter,
      &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_av1_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  GstD3D11AV1DecClass *klass = GST_D3D11_AV1_DEC_GET_CLASS (self);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), klass->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11device");
    return FALSE;
  }

  self->d3d11_decoder = gst_d3d11_decoder_new (self->device);

  if (!self->d3d11_decoder) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11 decoder");
    gst_clear_object (&self->device);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_av1_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_av1_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);

  if (!gst_d3d11_decoder_negotiate (self->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_av1_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);

  if (!gst_d3d11_decoder_decide_allocation (self->d3d11_decoder,
          decoder, query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_av1_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (decoder),
              query, self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static gboolean
gst_d3d11_av1_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (self->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (self->d3d11_decoder, decoder, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (self->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (self->d3d11_decoder, decoder, FALSE);
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (decoder, event);
}

static gboolean
gst_d3d11_av1_dec_new_sequence (GstAV1Decoder * decoder,
    const GstAV1SequenceHeaderOBU * seq_hdr)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  gboolean modified = FALSE;
  guint max_width, max_height;

  GST_LOG_OBJECT (self, "new sequence");

  if (seq_hdr->seq_profile != GST_AV1_PROFILE_0) {
    GST_WARNING_OBJECT (self, "Unsupported profile %d", seq_hdr->seq_profile);
    return FALSE;
  }

  if (seq_hdr->num_planes != 3) {
    GST_WARNING_OBJECT (self, "Monochrome is not supported");
    return FALSE;
  }

  self->seq_hdr = *seq_hdr;

  if (self->bitdepth != seq_hdr->bit_depth) {
    GST_INFO_OBJECT (self, "Bitdepth changed %d -> %d", self->bitdepth,
        seq_hdr->bit_depth);
    self->bitdepth = seq_hdr->bit_depth;
    modified = TRUE;
  }

  max_width = seq_hdr->max_frame_width_minus_1 + 1;
  max_height = seq_hdr->max_frame_height_minus_1 + 1;

  if (self->max_width != max_width || self->max_height != max_height) {
    GST_INFO_OBJECT (self, "Resolution changed %dx%d -> %dx%d",
        self->max_width, self->max_height, max_width, max_height);
    self->max_width = max_width;
    self->max_height = max_height;
    modified = TRUE;
  }

  if (modified || !gst_d3d11_decoder_is_configured (self->d3d11_decoder)) {
    GstVideoInfo info;
    GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (self->bitdepth == 8) {
      out_format = GST_VIDEO_FORMAT_NV12;
    } else if (self->bitdepth == 10) {
      out_format = GST_VIDEO_FORMAT_P010_10LE;
    } else {
      GST_WARNING_OBJECT (self, "Invalid bit-depth %d", seq_hdr->bit_depth);
      return FALSE;
    }

    gst_video_info_set_format (&info,
        out_format, self->max_width, self->max_height);

    if (!gst_d3d11_decoder_configure (self->d3d11_decoder, GST_D3D11_CODEC_AV1,
            decoder->input_state, &info, (gint) self->max_width,
            (gint) self->max_height, NUM_OUTPUT_VIEW)) {
      GST_ERROR_OBJECT (self, "Failed to create decoder");
      return FALSE;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_d3d11_av1_dec_new_picture (GstAV1Decoder * decoder,
    GstVideoCodecFrame * frame, GstAV1Picture * picture)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (self->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_av1_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New AV1 picture %p", picture);

  return TRUE;
}

static GstAV1Picture *
gst_d3d11_av1_dec_duplicate_picture (GstAV1Decoder * decoder,
    GstAV1Picture * picture)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  GstBuffer *view_buffer;
  GstAV1Picture *new_picture;

  view_buffer = (GstBuffer *) gst_av1_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Parent picture does not have output view buffer");
    return NULL;
  }

  new_picture = gst_av1_picture_new ();

  GST_LOG_OBJECT (self, "Duplicate output with buffer %" GST_PTR_FORMAT,
      view_buffer);

  gst_av1_picture_set_user_data (new_picture,
      gst_buffer_ref (view_buffer), (GDestroyNotify) gst_buffer_unref);

  return new_picture;
}

static gboolean
gst_d3d11_av1_dec_get_bitstream_buffer (GstD3D11AV1Dec * self)
{
  GST_TRACE_OBJECT (self, "Getting bitstream buffer");
  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &self->remaining_buffer_size,
          (gpointer *) & self->bitstream_buffer_data)) {
    GST_ERROR_OBJECT (self, "Faild to get bitstream buffer");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Got bitstream buffer %p with size %d",
      self->bitstream_buffer_data, self->remaining_buffer_size);
  self->written_buffer_size = 0;

  return TRUE;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_av1_dec_get_output_view_from_picture (GstD3D11AV1Dec * self,
    GstAV1Picture * picture, guint8 * view_id)
{
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_av1_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view =
      gst_d3d11_decoder_get_output_view_from_buffer (self->d3d11_decoder,
      view_buffer, view_id);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static gboolean
gst_d3d11_av1_dec_start_picture (GstAV1Decoder * decoder,
    GstAV1Picture * picture, GstAV1Dpb * dpb)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  const GstAV1SequenceHeaderOBU *seq_hdr = &self->seq_hdr;
  const GstAV1FrameHeaderOBU *frame_hdr = &picture->frame_hdr;
  ID3D11VideoDecoderOutputView *view;
  GST_DXVA_PicParams_AV1 *pic_params = &self->pic_params;
  guint8 view_id = 0xff;
  guint i, j;

  view = gst_d3d11_av1_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Begin frame");

  if (!gst_d3d11_decoder_begin_frame (self->d3d11_decoder, view, 0, NULL)) {
    GST_ERROR_OBJECT (self, "Failed to begin frame");
    return FALSE;
  }

  memset (pic_params, 0, sizeof (GST_DXVA_PicParams_AV1));

  pic_params->width = frame_hdr->frame_width;
  pic_params->height = frame_hdr->frame_height;

  pic_params->max_width = seq_hdr->max_frame_width_minus_1 + 1;
  pic_params->max_height = seq_hdr->max_frame_height_minus_1 + 1;

  pic_params->CurrPicTextureIndex = view_id;
  pic_params->superres_denom = frame_hdr->superres_denom;
  pic_params->bitdepth = seq_hdr->bit_depth;
  pic_params->seq_profile = seq_hdr->seq_profile;

  /* TILES */
  pic_params->tiles.cols = frame_hdr->tile_info.tile_cols;
  pic_params->tiles.rows = frame_hdr->tile_info.tile_rows;
  pic_params->tiles.context_update_id =
      frame_hdr->tile_info.context_update_tile_id;

  for (i = 0; i < pic_params->tiles.cols; i++) {
    pic_params->tiles.widths[i] =
        frame_hdr->tile_info.width_in_sbs_minus_1[i] + 1;
  }

  for (i = 0; i < pic_params->tiles.rows; i++) {
    pic_params->tiles.heights[i] =
        frame_hdr->tile_info.height_in_sbs_minus_1[i] + 1;
  }

  /* CODING TOOLS */
  pic_params->coding.use_128x128_superblock = seq_hdr->use_128x128_superblock;
  pic_params->coding.intra_edge_filter = seq_hdr->enable_filter_intra;
  pic_params->coding.interintra_compound = seq_hdr->enable_interintra_compound;
  pic_params->coding.masked_compound = seq_hdr->enable_masked_compound;
  pic_params->coding.warped_motion = frame_hdr->allow_warped_motion;
  pic_params->coding.dual_filter = seq_hdr->enable_dual_filter;
  pic_params->coding.jnt_comp = seq_hdr->enable_jnt_comp;
  pic_params->coding.screen_content_tools =
      frame_hdr->allow_screen_content_tools;
  pic_params->coding.integer_mv = frame_hdr->force_integer_mv;
  pic_params->coding.cdef = seq_hdr->enable_cdef;
  pic_params->coding.restoration = seq_hdr->enable_restoration;
  pic_params->coding.film_grain = seq_hdr->film_grain_params_present;
  pic_params->coding.intrabc = frame_hdr->allow_intrabc;
  pic_params->coding.high_precision_mv = frame_hdr->allow_high_precision_mv;
  pic_params->coding.switchable_motion_mode =
      frame_hdr->is_motion_mode_switchable;
  pic_params->coding.filter_intra = seq_hdr->enable_filter_intra;
  pic_params->coding.disable_frame_end_update_cdf =
      frame_hdr->disable_frame_end_update_cdf;
  pic_params->coding.disable_cdf_update = frame_hdr->disable_cdf_update;
  pic_params->coding.reference_mode = frame_hdr->reference_select;
  pic_params->coding.skip_mode = frame_hdr->skip_mode_present;
  pic_params->coding.reduced_tx_set = frame_hdr->reduced_tx_set;
  pic_params->coding.superres = frame_hdr->use_superres;
  pic_params->coding.tx_mode = frame_hdr->tx_mode;
  pic_params->coding.use_ref_frame_mvs = frame_hdr->use_ref_frame_mvs;
  pic_params->coding.enable_ref_frame_mvs = seq_hdr->enable_ref_frame_mvs;
  pic_params->coding.reference_frame_update = 1;

  /* FORMAT */
  pic_params->format.frame_type = frame_hdr->frame_type;
  pic_params->format.show_frame = frame_hdr->show_frame;
  pic_params->format.showable_frame = frame_hdr->showable_frame;
  pic_params->format.subsampling_x = seq_hdr->color_config.subsampling_x;
  pic_params->format.subsampling_y = seq_hdr->color_config.subsampling_y;
  pic_params->format.mono_chrome = seq_hdr->color_config.mono_chrome;

  /* REFERENCES */
  pic_params->primary_ref_frame = frame_hdr->primary_ref_frame;
  pic_params->order_hint = frame_hdr->order_hint;
  if (seq_hdr->enable_order_hint) {
    pic_params->order_hint_bits = seq_hdr->order_hint_bits_minus_1 + 1;
  } else {
    pic_params->order_hint_bits = 0;
  }

  for (i = 0; i < GST_AV1_REFS_PER_FRAME; i++) {
    if (dpb->pic_list[i]) {
      GstAV1Picture *other_pic = dpb->pic_list[i];
      const GstAV1GlobalMotionParams *gmp = &frame_hdr->global_motion_params;

      pic_params->frame_refs[i].width = other_pic->frame_hdr.frame_width;
      pic_params->frame_refs[i].height = other_pic->frame_hdr.frame_height;
      for (j = 0; j < 6; j++) {
        pic_params->frame_refs[i].wmmat[j] =
            gmp->gm_params[GST_AV1_REF_LAST_FRAME + i][j];
      }
      pic_params->frame_refs[i].wminvalid =
          (gmp->gm_type[GST_AV1_REF_LAST_FRAME + i] ==
          GST_AV1_WARP_MODEL_IDENTITY);
      pic_params->frame_refs[i].wmtype =
          gmp->gm_type[GST_AV1_REF_LAST_FRAME + i];
      pic_params->frame_refs[i].Index = frame_hdr->ref_frame_idx[i];
    } else {
      pic_params->frame_refs[i].Index = 0xff;
    }
  }

  for (i = 0; i < GST_AV1_NUM_REF_FRAMES; i++) {
    if (dpb->pic_list[i]) {
      GstAV1Picture *other_pic = dpb->pic_list[i];
      ID3D11VideoDecoderOutputView *other_view;
      guint8 other_view_id = 0xff;

      other_view = gst_d3d11_av1_dec_get_output_view_from_picture (self,
          other_pic, &other_view_id);
      if (!other_view) {
        GST_ERROR_OBJECT (self,
            "current picture does not have output view handle");
        return FALSE;
      }

      pic_params->RefFrameMapTextureIndex[i] = other_view_id;
    } else {
      pic_params->RefFrameMapTextureIndex[i] = 0xff;
    }
  }

  /* LOOP FILTER PARAMS */
  pic_params->loop_filter.filter_level[0] =
      frame_hdr->loop_filter_params.loop_filter_level[0];
  pic_params->loop_filter.filter_level[1] =
      frame_hdr->loop_filter_params.loop_filter_level[1];
  pic_params->loop_filter.filter_level_u =
      frame_hdr->loop_filter_params.loop_filter_level[2];
  pic_params->loop_filter.filter_level_v =
      frame_hdr->loop_filter_params.loop_filter_level[3];
  pic_params->loop_filter.sharpness_level =
      frame_hdr->loop_filter_params.loop_filter_sharpness;
  pic_params->loop_filter.mode_ref_delta_enabled =
      frame_hdr->loop_filter_params.loop_filter_delta_enabled;
  pic_params->loop_filter.mode_ref_delta_update =
      frame_hdr->loop_filter_params.loop_filter_delta_update;
  pic_params->loop_filter.delta_lf_multi =
      frame_hdr->loop_filter_params.delta_lf_multi;
  pic_params->loop_filter.delta_lf_present =
      frame_hdr->loop_filter_params.delta_lf_present;

  for (i = 0; i < GST_AV1_TOTAL_REFS_PER_FRAME; i++) {
    pic_params->loop_filter.ref_deltas[i] =
        frame_hdr->loop_filter_params.loop_filter_ref_deltas[i];
  }

  for (i = 0; i < 2; i++) {
    pic_params->loop_filter.mode_deltas[i] =
        frame_hdr->loop_filter_params.loop_filter_mode_deltas[i];
  }

  pic_params->loop_filter.delta_lf_res =
      frame_hdr->loop_filter_params.delta_lf_res;

  for (i = 0; i < GST_AV1_MAX_NUM_PLANES; i++) {
    pic_params->loop_filter.frame_restoration_type[i] =
        frame_hdr->loop_restoration_params.frame_restoration_type[i];
  }

  if (frame_hdr->loop_restoration_params.uses_lr) {
    pic_params->loop_filter.log2_restoration_unit_size[0] =
        (6 + frame_hdr->loop_restoration_params.lr_unit_shift);
    pic_params->loop_filter.log2_restoration_unit_size[1] =
        pic_params->loop_filter.log2_restoration_unit_size[2] =
        (6 + frame_hdr->loop_restoration_params.lr_unit_shift -
        frame_hdr->loop_restoration_params.lr_uv_shift);
  } else {
    pic_params->loop_filter.log2_restoration_unit_size[0] =
        pic_params->loop_filter.log2_restoration_unit_size[1] =
        pic_params->loop_filter.log2_restoration_unit_size[2] = 8;
  }

  /* QUANTIZATION */
  pic_params->quantization.delta_q_present =
      frame_hdr->quantization_params.delta_q_present;
  pic_params->quantization.delta_q_res =
      frame_hdr->quantization_params.delta_q_res;
  pic_params->quantization.base_qindex =
      frame_hdr->quantization_params.base_q_idx;
  pic_params->quantization.y_dc_delta_q =
      frame_hdr->quantization_params.delta_q_y_dc;
  pic_params->quantization.u_dc_delta_q =
      frame_hdr->quantization_params.delta_q_u_dc;
  pic_params->quantization.v_dc_delta_q =
      frame_hdr->quantization_params.delta_q_v_dc;
  pic_params->quantization.u_ac_delta_q =
      frame_hdr->quantization_params.delta_q_u_ac;
  pic_params->quantization.v_ac_delta_q =
      frame_hdr->quantization_params.delta_q_v_ac;
  if (frame_hdr->quantization_params.using_qmatrix) {
    pic_params->quantization.qm_y = frame_hdr->quantization_params.qm_y;
    pic_params->quantization.qm_u = frame_hdr->quantization_params.qm_u;
    pic_params->quantization.qm_v = frame_hdr->quantization_params.qm_v;
  } else {
    pic_params->quantization.qm_y = 0xff;
    pic_params->quantization.qm_u = 0xff;
    pic_params->quantization.qm_v = 0xff;
  }

  /* Cdef params */
  pic_params->cdef.damping = frame_hdr->cdef_params.cdef_damping - 3;
  pic_params->cdef.bits = frame_hdr->cdef_params.cdef_bits;

  for (i = 0; i < GST_AV1_CDEF_MAX; i++) {
    pic_params->cdef.y_strengths[i].primary =
        frame_hdr->cdef_params.cdef_y_pri_strength[i];
    pic_params->cdef.y_strengths[i].secondary =
        frame_hdr->cdef_params.cdef_y_sec_strength[i];

    pic_params->cdef.uv_strengths[i].primary =
        frame_hdr->cdef_params.cdef_uv_pri_strength[i];
    pic_params->cdef.uv_strengths[i].secondary =
        frame_hdr->cdef_params.cdef_uv_sec_strength[i];
  }

  pic_params->interp_filter = frame_hdr->interpolation_filter;

  /* SEGMENTATION */
  pic_params->segmentation.enabled =
      frame_hdr->segmentation_params.segmentation_enabled;
  pic_params->segmentation.update_map =
      frame_hdr->segmentation_params.segmentation_update_map;
  pic_params->segmentation.update_data =
      frame_hdr->segmentation_params.segmentation_update_data;
  pic_params->segmentation.temporal_update =
      frame_hdr->segmentation_params.segmentation_temporal_update;

  for (i = 0; i < GST_AV1_MAX_SEGMENTS; i++) {
    for (j = 0; j < GST_AV1_SEG_LVL_MAX; j++) {
      pic_params->segmentation.feature_mask[i].mask |=
          (frame_hdr->segmentation_params.feature_enabled[i][j] << j);
      pic_params->segmentation.feature_data[i][j] =
          frame_hdr->segmentation_params.feature_data[i][j];
    }
  }

  /* FILM GRAIN */
  if (frame_hdr->film_grain_params.apply_grain) {
    pic_params->film_grain.apply_grain = 1;
    pic_params->film_grain.scaling_shift_minus8 =
        frame_hdr->film_grain_params.grain_scaling_minus_8;
    pic_params->film_grain.chroma_scaling_from_luma =
        frame_hdr->film_grain_params.chroma_scaling_from_luma;
    pic_params->film_grain.ar_coeff_lag =
        frame_hdr->film_grain_params.ar_coeff_lag;
    pic_params->film_grain.ar_coeff_shift_minus6 =
        frame_hdr->film_grain_params.ar_coeff_shift_minus_6;
    pic_params->film_grain.grain_scale_shift =
        frame_hdr->film_grain_params.grain_scale_shift;
    pic_params->film_grain.overlap_flag =
        frame_hdr->film_grain_params.overlap_flag;
    pic_params->film_grain.clip_to_restricted_range =
        frame_hdr->film_grain_params.clip_to_restricted_range;
    pic_params->film_grain.matrix_coeff_is_identity =
        (seq_hdr->color_config.matrix_coefficients == GST_AV1_MC_IDENTITY);
    pic_params->film_grain.grain_seed = frame_hdr->film_grain_params.grain_seed;
    for (i = 0; i < frame_hdr->film_grain_params.num_y_points && i < 14; i++) {
      pic_params->film_grain.scaling_points_y[i][0] =
          frame_hdr->film_grain_params.point_y_value[i];
      pic_params->film_grain.scaling_points_y[i][1] =
          frame_hdr->film_grain_params.point_y_scaling[i];
    }
    pic_params->film_grain.num_y_points =
        frame_hdr->film_grain_params.num_y_points;

    for (i = 0; i < frame_hdr->film_grain_params.num_cb_points && i < 10; i++) {
      pic_params->film_grain.scaling_points_cb[i][0] =
          frame_hdr->film_grain_params.point_cb_value[i];
      pic_params->film_grain.scaling_points_cb[i][1] =
          frame_hdr->film_grain_params.point_cb_scaling[i];
    }
    pic_params->film_grain.num_cb_points =
        frame_hdr->film_grain_params.num_cb_points;

    for (i = 0; i < frame_hdr->film_grain_params.num_cr_points && i < 10; i++) {
      pic_params->film_grain.scaling_points_cr[i][0] =
          frame_hdr->film_grain_params.point_cr_value[i];
      pic_params->film_grain.scaling_points_cr[i][1] =
          frame_hdr->film_grain_params.point_cr_scaling[i];
    }
    pic_params->film_grain.num_cr_points =
        frame_hdr->film_grain_params.num_cr_points;

    for (i = 0; i < 24; i++) {
      pic_params->film_grain.ar_coeffs_y[i] =
          frame_hdr->film_grain_params.ar_coeffs_y_plus_128[i];
    }

    for (i = 0; i < 25; i++) {
      pic_params->film_grain.ar_coeffs_cb[i] =
          frame_hdr->film_grain_params.ar_coeffs_cb_plus_128[i];
      pic_params->film_grain.ar_coeffs_cr[i] =
          frame_hdr->film_grain_params.ar_coeffs_cr_plus_128[i];
    }

    pic_params->film_grain.cb_mult = frame_hdr->film_grain_params.cb_mult;
    pic_params->film_grain.cb_luma_mult =
        frame_hdr->film_grain_params.cb_luma_mult;
    pic_params->film_grain.cr_mult = frame_hdr->film_grain_params.cr_mult;
    pic_params->film_grain.cr_luma_mult =
        frame_hdr->film_grain_params.cr_luma_mult;
    pic_params->film_grain.cb_offset = frame_hdr->film_grain_params.cb_offset;
    pic_params->film_grain.cr_offset = frame_hdr->film_grain_params.cr_offset;
  }

  return gst_d3d11_av1_dec_get_bitstream_buffer (self);
}

static gboolean
gst_d3d11_av1_dec_decode_tile (GstAV1Decoder * decoder,
    GstAV1Picture * picture, GstAV1Tile * tile)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  GstAV1TileGroupOBU *tile_group = &tile->tile_group;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  guint i;

  view = gst_d3d11_av1_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  if (tile_group->num_tiles > self->tile_list->len)
    g_array_set_size (self->tile_list, tile_group->num_tiles);

  g_assert (tile_group->tg_end < self->tile_list->len);

  if (tile->obu.obu_size > self->remaining_buffer_size) {
    GST_ERROR_OBJECT (self, "Too large OBU size");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "Decode tile, tile count %d (start: %d - end: %d)",
      tile_group->num_tiles, tile_group->tg_start, tile_group->tg_end);

  for (i = tile_group->tg_start; i <= tile_group->tg_end; i++) {
    GST_DXVA_Tile_AV1 *dxva_tile =
        &g_array_index (self->tile_list, GST_DXVA_Tile_AV1, i);

    GST_TRACE_OBJECT (self,
        "Written size %d, Tile offset %d, size %d, row %d, col %d",
        self->written_buffer_size,
        tile_group->entry[i].tile_offset, tile_group->entry[i].tile_size,
        tile_group->entry[i].tile_row, tile_group->entry[i].tile_col);

    dxva_tile->DataOffset =
        self->written_buffer_size + tile_group->entry[i].tile_offset;
    dxva_tile->DataSize = tile_group->entry[i].tile_size;
    dxva_tile->row = tile_group->entry[i].tile_row;
    dxva_tile->column = tile_group->entry[i].tile_col;
    /* TODO: used for tile list OBU */
    dxva_tile->anchor_frame = 0xff;
  }

  GST_TRACE_OBJECT (self, "OBU size %d", tile->obu.obu_size);

  memcpy (self->bitstream_buffer_data, tile->obu.data, tile->obu.obu_size);
  self->remaining_buffer_size -= tile->obu.obu_size;
  self->bitstream_buffer_data += tile->obu.obu_size;
  self->written_buffer_size += tile->obu.obu_size;

  return TRUE;
}

static gboolean
gst_d3d11_av1_dec_end_picture (GstAV1Decoder * decoder, GstAV1Picture * picture)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  guint d3d11_buffer_size;
  gpointer d3d11_buffer;
  guint padding;
  D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[3];
  guint i;
  guint8 *data;
  gsize offset = 0;

  GST_TRACE_OBJECT (self, "Getting picture params buffer");
  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &d3d11_buffer_size,
          &d3d11_buffer)) {
    GST_ERROR_OBJECT (self,
        "Failed to get decoder buffer for picture parameters");
    return FALSE;
  }

  if (d3d11_buffer_size < sizeof (GST_DXVA_PicParams_AV1)) {
    GST_ERROR_OBJECT (self,
        "Too small picture param buffer %d", d3d11_buffer_size);

    gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
        D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS);
    return FALSE;
  }

  memcpy (d3d11_buffer, &self->pic_params, sizeof (GST_DXVA_PicParams_AV1));

  GST_TRACE_OBJECT (self, "Release picture param decoder buffer");

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS)) {
    GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Getting slice control buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &d3d11_buffer_size,
          &d3d11_buffer)) {
    GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");
    return FALSE;
  }

  if (d3d11_buffer_size < sizeof (GST_DXVA_Tile_AV1) * self->tile_list->len) {
    GST_ERROR_OBJECT (self, "Too small slice control buffer %d",
        d3d11_buffer_size);
    gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
        D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL);
    return FALSE;
  }

  data = (guint8 *) d3d11_buffer;
  for (i = 0; i < self->tile_list->len; i++) {
    GST_DXVA_Tile_AV1 *dxva_tile =
        &g_array_index (self->tile_list, GST_DXVA_Tile_AV1, i);

    memcpy (data + offset, dxva_tile, sizeof (GST_DXVA_Tile_AV1));
    offset += sizeof (GST_DXVA_Tile_AV1);
  }

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL)) {
    GST_ERROR_OBJECT (self, "Failed to release slice control buffer");
    return FALSE;
  }

  padding = MIN (GST_ROUND_UP_128 (self->written_buffer_size) -
      self->written_buffer_size, self->remaining_buffer_size);
  if (padding) {
    memset (self->bitstream_buffer_data, 0, padding);
    self->written_buffer_size += padding;
  }

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM)) {
    GST_ERROR_OBJECT (self, "Failed to release bitstream buffer");

    return FALSE;
  }

  buffer_desc[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
  buffer_desc[0].DataOffset = 0;
  buffer_desc[0].DataSize = sizeof (GST_DXVA_PicParams_AV1);

  buffer_desc[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
  buffer_desc[1].DataOffset = 0;
  buffer_desc[1].DataSize = sizeof (GST_DXVA_Tile_AV1) * self->tile_list->len;

  buffer_desc[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
  buffer_desc[2].DataOffset = 0;
  buffer_desc[2].DataSize = self->written_buffer_size;

  if (!gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
          3, buffer_desc)) {
    GST_ERROR_OBJECT (self, "Couldn't submit decoder buffers");
    return FALSE;
  }

  if (!gst_d3d11_decoder_end_frame (self->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to EndFrame");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_d3d11_av1_dec_output_picture (GstAV1Decoder * decoder,
    GstVideoCodecFrame * frame, GstAV1Picture * picture)
{
  GstD3D11AV1Dec *self = GST_D3D11_AV1_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p, %dx%d", picture,
      picture->frame_hdr.render_width, picture->frame_hdr.render_height);

  view_buffer = (GstBuffer *) gst_av1_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!gst_d3d11_decoder_process_output (self->d3d11_decoder, vdec,
          picture->frame_hdr.render_width, picture->frame_hdr.render_height,
          view_buffer, &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  gst_av1_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_av1_picture_unref (picture);
  gst_video_decoder_drop_frame (vdec, frame);

  return GST_FLOW_ERROR;
}

typedef struct
{
  guint width;
  guint height;
} GstD3D11AV1DecResolution;

void
gst_d3d11_av1_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    GstD3D11Decoder * decoder, guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  GTypeInfo type_info = {
    sizeof (GstD3D11AV1DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_av1_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11AV1Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_av1_dec_init,
  };
  const GUID *profile_guid = NULL;
  /* values were taken from chromium. See supported_profile_helper.cc */
  GstD3D11AV1DecResolution resolutions_to_check[] = {
    {4096, 2160}, {4096, 2304}, {7680, 4320}, {8192, 4320}, {8192, 8192}
  };
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  gboolean have_p010 = FALSE;
  gboolean have_gray = FALSE;
  gboolean have_gray10 = FALSE;

  if (!gst_d3d11_decoder_get_supported_decoder_profile (decoder,
          GST_D3D11_CODEC_AV1, GST_VIDEO_FORMAT_NV12, &profile_guid)) {
    GST_INFO_OBJECT (device, "device does not support VP8 decoding");
    return;
  }

  have_p010 = gst_d3d11_decoder_supports_format (decoder,
      profile_guid, DXGI_FORMAT_P010);
  have_gray = gst_d3d11_decoder_supports_format (decoder,
      profile_guid, DXGI_FORMAT_R8_UNORM);
  have_gray10 = gst_d3d11_decoder_supports_format (decoder,
      profile_guid, DXGI_FORMAT_R16_UNORM);

  GST_INFO_OBJECT (device, "Decoder support P010: %d, R8: %d, R16: %d",
      have_p010, have_gray, have_gray10);

  /* TODO: add test monochrome formats */
  for (i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
    if (gst_d3d11_decoder_supports_resolution (decoder, profile_guid,
            DXGI_FORMAT_NV12, resolutions_to_check[i].width,
            resolutions_to_check[i].height)) {
      max_width = resolutions_to_check[i].width;
      max_height = resolutions_to_check[i].height;

      GST_DEBUG_OBJECT (device,
          "device support resolution %dx%d", max_width, max_height);
    } else {
      break;
    }
  }

  if (max_width == 0 || max_height == 0) {
    GST_WARNING_OBJECT (device, "Couldn't query supported resolution");
    return;
  }

  sink_caps =
      gst_caps_from_string ("video/x-av1, "
      "alignment = (string) frame, profile = (string) 0");
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "); video/x-raw");

  if (have_p010) {
    GValue format_list = G_VALUE_INIT;
    GValue format_value = G_VALUE_INIT;

    g_value_init (&format_list, GST_TYPE_LIST);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "NV12");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "P010_10LE");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    gst_caps_set_value (src_caps, "format", &format_list);
    g_value_unset (&format_list);
  } else {
    gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);
  }

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 1, resolution,
      "height", GST_TYPE_INT_RANGE, 1, resolution, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 1, resolution,
      "height", GST_TYPE_INT_RANGE, 1, resolution, NULL);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, sink_caps, src_caps);

  type_name = g_strdup ("GstD3D11AV1Dec");
  feature_name = g_strdup ("d3d11av1dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11AV1Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11av1device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_AV1_DECODER,
      type_name, &type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
