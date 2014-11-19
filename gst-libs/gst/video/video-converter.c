/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-converter.h"

#include <glib.h>
#include <string.h>
#include <math.h>

#include "video-orc.h"

/**
 * SECTION:videoconverter
 * @short_description: Generic video conversion
 *
 * <refsect2>
 * <para>
 * This object is used to convert video frames from one format to another.
 * The object can perform conversion of:
 * <itemizedlist>
 *  <listitem><para>
 *    video format
 *  </para></listitem>
 *  <listitem><para>
 *    video colorspace
 *  </para></listitem>
 *  <listitem><para>
 *    chroma-siting
 *  </para></listitem>
 *  <listitem><para>
 *    video size (planned)
 *  </para></listitem>
 * </para>
 * </refsect2>
 */

/*
 * (a)  unpack
 * (b)  chroma upsample
 * (c)  (convert Y'CbCr to R'G'B')
 * (d)  gamma decode
 * (e)  downscale
 * (f)  colorspace convert through XYZ
 * (g)  upscale
 * (h)  gamma encode
 * (i)  (convert R'G'B' to Y'CbCr)
 * (j)  chroma downsample
 * (k)  pack
 *
 * quality options
 *
 *  (a) range truncate, range expand
 *  (b) full upsample, 1-1 non-cosited upsample, no upsample
 *  (c) 8 bits, 16 bits
 *  (d)
 *  (e) 8 bits, 16 bits
 *  (f) 8 bits, 16 bits
 *  (g) 8 bits, 16 bits
 *  (h)
 *  (i) 8 bits, 16 bits
 *  (j) 1-1 cosited downsample, no downsample
 *  (k)
 *
 *
 *         1 : a ->   ->   ->   -> e  -> f  -> g  ->   ->   ->   -> k
 *         2 : a ->   ->   ->   -> e  -> f* -> g  ->   ->   ->   -> k
 *         3 : a ->   ->   ->   -> e* -> f* -> g* ->   ->   ->   -> k
 *         4 : a -> b ->   ->   -> e  -> f  -> g  ->   ->   -> j -> k
 *         5 : a -> b ->   ->   -> e* -> f* -> g* ->   ->   -> j -> k
 *         6 : a -> b -> c -> d -> e  -> f  -> g  -> h -> i -> j -> k
 *         7 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 *
 *         8 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 *         9 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 *        10 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 */
typedef struct _GstLineCache GstLineCache;

#define SCALE    (8)
#define SCALE_F  ((float) (1 << SCALE))

typedef struct _MatrixData MatrixData;

struct _MatrixData
{
  gdouble dm[4][4];
  gint im[4][4];
  gint width;
  guint64 orc_p1;
  guint64 orc_p2;
  guint64 orc_p3;
  void (*matrix_func) (MatrixData * data, gpointer pixels);
};

typedef struct _GammaData GammaData;

struct _GammaData
{
  gpointer gamma_table;
  gint width;
  void (*gamma_func) (GammaData * data, gpointer dest, gpointer src);
};

struct _GstVideoConverter
{
  gint flags;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  gint in_x;
  gint in_y;
  gint in_width;
  gint in_height;
  gint in_maxwidth;
  gint in_maxheight;
  gint out_x;
  gint out_y;
  gint out_width;
  gint out_height;
  gint out_maxwidth;
  gint out_maxheight;

  gint current_pstride;
  gint current_width;
  gint current_height;
  GstVideoFormat current_format;
  gint current_bits;

  GstStructure *config;
  GstVideoDitherMethod dither;

  guint n_tmplines;
  gpointer *tmplines;
  guint16 *errline;
  guint tmplines_idx;

  guint n_btmplines;
  gpointer *btmplines;
  guint btmplines_idx;

  gboolean fill_border;
  gpointer borderline;
  guint32 border_argb;

  void (*convert) (GstVideoConverter * convert, const GstVideoFrame * src,
      GstVideoFrame * dest);
  void (*dither16) (GstVideoConverter * convert, guint16 * pixels, int j);

  /* data for unpack */
  GstLineCache *unpack_lines;
  GstVideoFormat unpack_format;
  guint unpack_bits;
  gboolean unpack_rgb;
  gboolean identity_unpack;
  gint unpack_pstride;

  /* chroma upsample */
  GstLineCache *upsample_lines;
  GstVideoChromaResample *upsample;
  guint up_n_lines;
  gint up_offset;

  /* to R'G'B */
  GstLineCache *to_RGB_lines;
  MatrixData to_RGB_matrix;
  /* gamma decode */
  GammaData gamma_dec;

  /* scaling */
  GstLineCache *hscale_lines;
  GstVideoScaler *h_scaler;
  gint h_scale_format;
  GstLineCache *vscale_lines;
  GstVideoScaler *v_scaler;
  gint v_scale_width;
  gint v_scale_format;

  /* color space conversion */
  GstLineCache *convert_lines;
  MatrixData convert_matrix;
  gint in_bits;
  gint out_bits;

  /* gamma encode */
  GammaData gamma_enc;
  /* to Y'CbCr */
  GstLineCache *to_YUV_lines;
  MatrixData to_YUV_matrix;

  /* chroma downsample */
  GstLineCache *downsample_lines;
  GstVideoChromaResample *downsample;
  guint down_n_lines;
  gint down_offset;

  /* pack */
  GstLineCache *pack_lines;
  guint pack_nlines;
  GstVideoFormat pack_format;
  guint pack_bits;
  gboolean pack_rgb;
  gboolean identity_pack;
  gint pack_pstride;

  const GstVideoFrame *src;
  GstVideoFrame *dest;
};

typedef gpointer (*GstLineCacheAllocLineFunc) (GstLineCache * cache, gint idx,
    gpointer user_data);
typedef gboolean (*GstLineCacheNeedLineFunc) (GstLineCache * cache,
    gint out_line, gint in_line, gpointer user_data);

struct _GstLineCache
{
  gint first;
  GPtrArray *lines;

  GstLineCache *prev;
  gboolean write_input;
  gboolean pass_alloc;
  gboolean alloc_writable;
  GstLineCacheNeedLineFunc need_line;
  gpointer need_line_data;
  GDestroyNotify need_line_notify;
  GstLineCacheAllocLineFunc alloc_line;
  gpointer alloc_line_data;
  GDestroyNotify alloc_line_notify;
};

static GstLineCache *
gst_line_cache_new (GstLineCache * prev)
{
  GstLineCache *result;

  result = g_slice_new0 (GstLineCache);
  result->lines = g_ptr_array_new ();
  result->prev = prev;

  return result;
}

static void
gst_line_cache_clear (GstLineCache * cache)
{
  g_return_if_fail (cache != NULL);

  g_ptr_array_set_size (cache->lines, 0);
  cache->first = 0;
}

static void
gst_line_cache_free (GstLineCache * cache)
{
  gst_line_cache_clear (cache);
  g_ptr_array_unref (cache->lines);
  g_slice_free (GstLineCache, cache);
}

static void
gst_line_cache_set_need_line_func (GstLineCache * cache,
    GstLineCacheNeedLineFunc need_line, gpointer user_data,
    GDestroyNotify notify)
{
  cache->need_line = need_line;
  cache->need_line_data = user_data;
  cache->need_line_notify = notify;
}

static void
gst_line_cache_set_alloc_line_func (GstLineCache * cache,
    GstLineCacheAllocLineFunc alloc_line, gpointer user_data,
    GDestroyNotify notify)
{
  cache->alloc_line = alloc_line;
  cache->alloc_line_data = user_data;
  cache->alloc_line_notify = notify;
}

/* keep this much backlog */
#define BACKLOG 2

static gpointer *
gst_line_cache_get_lines (GstLineCache * cache, gint out_line, gint in_line,
    gint n_lines)
{
  if (cache->first + BACKLOG < in_line) {
    gint to_remove =
        MIN (in_line - (cache->first + BACKLOG), cache->lines->len);
    if (to_remove > 0) {
      g_ptr_array_remove_range (cache->lines, 0, to_remove);
      cache->first += to_remove;
    }
  } else if (in_line < cache->first) {
    gst_line_cache_clear (cache);
    cache->first = in_line;
  }

  while (TRUE) {
    gint oline;

    if (cache->first <= in_line
        && in_line + n_lines <= cache->first + (gint) cache->lines->len) {
      return cache->lines->pdata + (in_line - cache->first);
    }

    if (cache->need_line == NULL)
      break;

    oline = out_line + cache->first + cache->lines->len - in_line;

    if (!cache->need_line (cache, oline, cache->first + cache->lines->len,
            cache->need_line_data))
      break;
  }
  GST_DEBUG ("no lines");
  return NULL;
}

static void
gst_line_cache_add_line (GstLineCache * cache, gint idx, gpointer line)
{
  if (cache->first + cache->lines->len != idx) {
    gst_line_cache_clear (cache);
    cache->first = idx;
  }
  g_ptr_array_add (cache->lines, line);
}

static gpointer
gst_line_cache_alloc_line (GstLineCache * cache, gint idx)
{
  gpointer res;

  if (cache->alloc_line)
    res = cache->alloc_line (cache, idx, cache->alloc_line_data);
  else
    res = NULL;

  return res;
}

static void video_converter_generic (GstVideoConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest);
static gboolean video_converter_lookup_fastpath (GstVideoConverter * convert);
static void video_converter_compute_matrix (GstVideoConverter * convert);
static void video_converter_compute_resample (GstVideoConverter * convert);

static gpointer get_dest_line (GstLineCache * cache, gint idx,
    gpointer user_data);

static gboolean do_unpack_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_downsample_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_convert_to_RGB_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_convert_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_convert_to_YUV_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_upsample_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_vscale_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_hscale_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);

static void
alloc_tmplines (GstVideoConverter * convert, guint lines, guint blines,
    gint width)
{
  gint i;

  convert->n_tmplines = lines;
  convert->tmplines = g_malloc (lines * sizeof (gpointer));
  for (i = 0; i < lines; i++)
    convert->tmplines[i] = g_malloc (sizeof (guint16) * (width + 8) * 4);
  convert->tmplines_idx = 0;

  convert->n_btmplines = blines;
  convert->btmplines = g_malloc (blines * sizeof (gpointer));
  for (i = 0; i < blines; i++) {
    convert->btmplines[i] = g_malloc (sizeof (guint16) * (width + 8) * 4);
    if (convert->borderline)
      memcpy (convert->btmplines[i], convert->borderline, width * 8);
  }
  convert->btmplines_idx = 0;
}

static gpointer
get_temp_line (GstLineCache * cache, gint idx, gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer tmpline;

  GST_DEBUG ("get temp line %d", idx);
  tmpline = (guint8 *) convert->tmplines[convert->tmplines_idx] +
      (convert->out_x * convert->pack_pstride);
  convert->tmplines_idx = (convert->tmplines_idx + 1) % convert->n_tmplines;

  return tmpline;
}

static gpointer
get_border_temp_line (GstLineCache * cache, gint idx, gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer tmpline;

  GST_DEBUG ("get border temp line %d", idx);
  tmpline = (guint8 *) convert->btmplines[convert->btmplines_idx] +
      (convert->out_x * convert->pack_pstride);
  convert->btmplines_idx = (convert->btmplines_idx + 1) % convert->n_btmplines;

  return tmpline;
}

static gboolean
check_str_option (GstVideoConverter * convert, const gchar * option,
    const gchar * value, gboolean def)
{
  const gchar *str;

  if ((str = gst_structure_get_string (convert->config, option)))
    return g_strcmp0 (str, value) == 0;

  return def;
}

#define CHECK_MATRIX_FULL(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_MATRIX_MODE, "full", TRUE)
#define CHECK_MATRIX_NO_YUV(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_MATRIX_MODE, "no-yuv", FALSE)

#define CHECK_GAMMA_NONE(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_GAMMA_MODE, "none", TRUE)
#define CHECK_GAMMA_REMAP(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_GAMMA_MODE, "remap", FALSE)

#define CHECK_PRIMARIES_NONE(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_PRIMARIES_MODE, "none", TRUE)
#define CHECK_PRIMARIES_MERGE(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_PRIMARIES_MODE, "merge-only", FALSE)
#define CHECK_PRIMARIES_FAST(c) check_str_option ((c),GST_VIDEO_CONVERTER_OPT_PRIMARIES_MODE, "fast", FALSE)

static GstLineCache *
chain_unpack_line (GstVideoConverter * convert)
{
  GstLineCache *prev;
  GstVideoInfo *info;

  info = &convert->in_info;

  convert->current_format = convert->unpack_format;
  convert->current_bits = convert->unpack_bits;
  convert->current_pstride = convert->current_bits >> 1;

  convert->unpack_pstride = convert->current_pstride;
  convert->identity_unpack = (convert->current_format == info->finfo->format);

  GST_DEBUG ("chain unpack line format %s, pstride %d, identity_unpack %d",
      gst_video_format_to_string (convert->current_format),
      convert->current_pstride, convert->identity_unpack);

  prev = convert->unpack_lines = gst_line_cache_new (NULL);
  prev->write_input = FALSE;
  prev->pass_alloc = FALSE;
  gst_line_cache_set_need_line_func (convert->unpack_lines,
      do_unpack_lines, convert, NULL);

  return convert->unpack_lines;
}

static GstLineCache *
chain_upsample (GstVideoConverter * convert, GstLineCache * prev)
{
  video_converter_compute_resample (convert);

  if (convert->upsample) {
    GST_DEBUG ("chain upsample");
    prev = convert->upsample_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = TRUE;
    gst_line_cache_set_need_line_func (convert->upsample_lines,
        do_upsample_lines, convert, NULL);
  }
  return prev;
}

static void
color_matrix_set_identity (MatrixData * m)
{
  int i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      m->dm[i][j] = (i == j);
    }
  }
}

static void
color_matrix_copy (MatrixData * d, const MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      d->dm[i][j] = s->dm[i][j];
}

/* Perform 4x4 matrix multiplication:
 *  - @dst@ = @a@ * @b@
 *  - @dst@ may be a pointer to @a@ andor @b@
 */
static void
color_matrix_multiply (MatrixData * dst, MatrixData * a, MatrixData * b)
{
  MatrixData tmp;
  int i, j, k;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      double x = 0;
      for (k = 0; k < 4; k++) {
        x += a->dm[i][k] * b->dm[k][j];
      }
      tmp.dm[i][j] = x;
    }
  }
  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_invert (MatrixData * d, MatrixData * s)
{
  MatrixData tmp;
  int i, j;
  double det;

  color_matrix_set_identity (&tmp);
  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      tmp.dm[j][i] =
          s->dm[(i + 1) % 3][(j + 1) % 3] * s->dm[(i + 2) % 3][(j + 2) % 3] -
          s->dm[(i + 1) % 3][(j + 2) % 3] * s->dm[(i + 2) % 3][(j + 1) % 3];
    }
  }
  det =
      tmp.dm[0][0] * s->dm[0][0] + tmp.dm[0][1] * s->dm[1][0] +
      tmp.dm[0][2] * s->dm[2][0];
  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      tmp.dm[i][j] /= det;
    }
  }
  color_matrix_copy (d, &tmp);
}

static void
color_matrix_offset_components (MatrixData * m, double a1, double a2, double a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][3] = a1;
  a.dm[1][3] = a2;
  a.dm[2][3] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_scale_components (MatrixData * m, double a1, double a2, double a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][0] = a1;
  a.dm[1][1] = a2;
  a.dm[2][2] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_debug (const MatrixData * s)
{
  GST_DEBUG ("[%f %f %f %f]", s->dm[0][0], s->dm[0][1], s->dm[0][2],
      s->dm[0][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[1][0], s->dm[1][1], s->dm[1][2],
      s->dm[1][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[2][0], s->dm[2][1], s->dm[2][2],
      s->dm[2][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[3][0], s->dm[3][1], s->dm[3][2],
      s->dm[3][3]);
}

static void
color_matrix_convert (MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      s->im[i][j] = rint (s->dm[i][j]);

  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[0][0], s->im[0][1], s->im[0][2],
      s->im[0][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[1][0], s->im[1][1], s->im[1][2],
      s->im[1][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[2][0], s->im[2][1], s->im[2][2],
      s->im[2][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[3][0], s->im[3][1], s->im[3][2],
      s->im[3][3]);
}

static void
color_matrix_YCbCr_to_RGB (MatrixData * m, double Kr, double Kb)
{
  double Kg = 1.0 - Kr - Kb;
  MatrixData k = {
    {
          {1., 0., 2 * (1 - Kr), 0.},
          {1., -2 * Kb * (1 - Kb) / Kg, -2 * Kr * (1 - Kr) / Kg, 0.},
          {1., 2 * (1 - Kb), 0., 0.},
          {0., 0., 0., 1.},
        }
  };

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_YCbCr (MatrixData * m, double Kr, double Kb)
{
  double Kg = 1.0 - Kr - Kb;
  MatrixData k;
  double x;

  k.dm[0][0] = Kr;
  k.dm[0][1] = Kg;
  k.dm[0][2] = Kb;
  k.dm[0][3] = 0;

  x = 1 / (2 * (1 - Kb));
  k.dm[1][0] = -x * Kr;
  k.dm[1][1] = -x * Kg;
  k.dm[1][2] = x * (1 - Kb);
  k.dm[1][3] = 0;

  x = 1 / (2 * (1 - Kr));
  k.dm[2][0] = x * (1 - Kr);
  k.dm[2][1] = -x * Kg;
  k.dm[2][2] = -x * Kb;
  k.dm[2][3] = 0;

  k.dm[3][0] = 0;
  k.dm[3][1] = 0;
  k.dm[3][2] = 0;
  k.dm[3][3] = 1;

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_XYZ (MatrixData * dst, double Rx, double Ry, double Gx,
    double Gy, double Bx, double By, double Wx, double Wy)
{
  MatrixData m, im;
  double sx, sy, sz;
  double wx, wy, wz;

  color_matrix_set_identity (&m);

  m.dm[0][0] = Rx;
  m.dm[1][0] = Ry;
  m.dm[2][0] = (1.0 - Rx - Ry);
  m.dm[0][1] = Gx;
  m.dm[1][1] = Gy;
  m.dm[2][1] = (1.0 - Gx - Gy);
  m.dm[0][2] = Bx;
  m.dm[1][2] = By;
  m.dm[2][2] = (1.0 - Bx - By);

  color_matrix_invert (&im, &m);

  wx = Wx / Wy;
  wy = 1.0;
  wz = (1.0 - Wx - Wy) / Wy;

  sx = im.dm[0][0] * wx + im.dm[0][1] * wy + im.dm[0][2] * wz;
  sy = im.dm[1][0] * wx + im.dm[1][1] * wy + im.dm[1][2] * wz;
  sz = im.dm[2][0] * wx + im.dm[2][1] * wy + im.dm[2][2] * wz;

  m.dm[0][0] *= sx;
  m.dm[1][0] *= sx;
  m.dm[2][0] *= sx;
  m.dm[0][1] *= sy;
  m.dm[1][1] *= sy;
  m.dm[2][1] *= sy;
  m.dm[0][2] *= sz;
  m.dm[1][2] *= sz;
  m.dm[2][2] *= sz;

  color_matrix_copy (dst, &m);
}

static void
video_converter_matrix8 (MatrixData * data, gpointer pixels)
{
#if 1
  video_orc_matrix8 (pixels, pixels, data->orc_p1, data->orc_p2,
      data->orc_p3, data->width);
#elif 0
  /* FIXME we would like to set this as a backup function, it's faster than the
   * orc generated one */
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *p = pixels;
  gint width = data->width;

  for (i = 0; i < width; i++) {
    r = p[i * 4 + 1];
    g = p[i * 4 + 2];
    b = p[i * 4 + 3];

    y = (data->im[0][0] * r + data->im[0][1] * g +
        data->im[0][2] * b + data->im[0][3]) >> SCALE;
    u = (data->im[1][0] * r + data->im[1][1] * g +
        data->im[1][2] * b + data->im[1][3]) >> SCALE;
    v = (data->im[2][0] * r + data->im[2][1] * g +
        data->im[2][2] * b + data->im[2][3]) >> SCALE;

    p[i * 4 + 1] = CLAMP (y, 0, 255);
    p[i * 4 + 2] = CLAMP (u, 0, 255);
    p[i * 4 + 3] = CLAMP (v, 0, 255);
  }
#endif
}

static void
video_converter_matrix8_AYUV_ARGB (MatrixData * data, gpointer pixels)
{
  video_orc_convert_AYUV_ARGB (pixels, 0, pixels, 0,
      data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], data->width, 1);
}

static gboolean
is_ayuv_to_rgb_matrix (MatrixData * data)
{
  if (data->im[0][0] != data->im[1][0] || data->im[1][0] != data->im[2][0])
    return FALSE;

  if (data->im[0][1] != 0 || data->im[2][2] != 0)
    return FALSE;

  return TRUE;
}

static void
video_converter_matrix16 (MatrixData * data, gpointer pixels)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *p = pixels;
  gint width = data->width;

  for (i = 0; i < width; i++) {
    r = p[i * 4 + 1];
    g = p[i * 4 + 2];
    b = p[i * 4 + 3];

    y = (data->im[0][0] * r + data->im[0][1] * g +
        data->im[0][2] * b + data->im[0][3]) >> SCALE;
    u = (data->im[1][0] * r + data->im[1][1] * g +
        data->im[1][2] * b + data->im[1][3]) >> SCALE;
    v = (data->im[2][0] * r + data->im[2][1] * g +
        data->im[2][2] * b + data->im[2][3]) >> SCALE;

    p[i * 4 + 1] = CLAMP (y, 0, 65535);
    p[i * 4 + 2] = CLAMP (u, 0, 65535);
    p[i * 4 + 3] = CLAMP (v, 0, 65535);
  }
}


static void
prepare_matrix (GstVideoConverter * convert, MatrixData * data)
{
  color_matrix_scale_components (data, SCALE_F, SCALE_F, SCALE_F);
  color_matrix_convert (data);

  data->width = convert->current_width;

  if (convert->current_bits == 8) {
    if (!convert->unpack_rgb && convert->pack_rgb
        && is_ayuv_to_rgb_matrix (data)) {
      GST_DEBUG ("use fast AYUV -> RGB matrix");
      data->matrix_func = video_converter_matrix8_AYUV_ARGB;
    } else {
      GST_DEBUG ("use 8bit matrix");
      data->matrix_func = video_converter_matrix8;

      data->orc_p1 = (((guint64) (guint16) data->im[2][0]) << 48) |
          (((guint64) (guint16) data->im[1][0]) << 32) |
          (((guint64) (guint16) data->im[0][0]) << 16);
      data->orc_p2 = (((guint64) (guint16) data->im[2][1]) << 48) |
          (((guint64) (guint16) data->im[1][1]) << 32) |
          (((guint64) (guint16) data->im[0][1]) << 16);
      data->orc_p3 = (((guint64) (guint16) data->im[2][2]) << 48) |
          (((guint64) (guint16) data->im[1][2]) << 32) |
          (((guint64) (guint16) data->im[0][2]) << 16);
    }
  } else {
    GST_DEBUG ("use 16bit matrix");
    data->matrix_func = video_converter_matrix16;
  }
}

static void
compute_matrix_to_RGB (GstVideoConverter * convert, MatrixData * data)
{
  GstVideoInfo *info;
  gdouble Kr = 0, Kb = 0;

  info = &convert->in_info;

  {
    const GstVideoFormatInfo *uinfo;
    gint offset[4], scale[4];

    uinfo = gst_video_format_get_info (convert->unpack_format);

    /* bring color components to [0..1.0] range */
    gst_video_color_range_offsets (info->colorimetry.range, uinfo, offset,
        scale);

    color_matrix_offset_components (data, -offset[0], -offset[1], -offset[2]);
    color_matrix_scale_components (data, 1 / ((float) scale[0]),
        1 / ((float) scale[1]), 1 / ((float) scale[2]));
  }

  /* bring components to R'G'B' space */
  if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
    color_matrix_YCbCr_to_RGB (data, Kr, Kb);

  color_matrix_debug (data);
}

static void
compute_matrix_to_YUV (GstVideoConverter * convert, MatrixData * data)
{
  GstVideoInfo *info;
  gdouble Kr = 0, Kb = 0;

  info = &convert->out_info;

  /* bring components to YCbCr space */
  if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
    color_matrix_RGB_to_YCbCr (data, Kr, Kb);

  {
    const GstVideoFormatInfo *uinfo;
    gint offset[4], scale[4];

    uinfo = gst_video_format_get_info (convert->pack_format);

    /* bring color components to nominal range */
    gst_video_color_range_offsets (info->colorimetry.range, uinfo, offset,
        scale);

    color_matrix_scale_components (data, (float) scale[0], (float) scale[1],
        (float) scale[2]);
    color_matrix_offset_components (data, offset[0], offset[1], offset[2]);
  }

  color_matrix_debug (data);
}


static void
gamma_convert_u8_u16 (GammaData * data, gpointer dest, gpointer src)
{
  gint i;
  guint8 *s = src;
  guint16 *d = dest;
  guint16 *table = data->gamma_table;
  gint width = data->width * 4;

  for (i = 0; i < width; i++)
    d[i] = table[s[i]];
}

static void
gamma_convert_u16_u8 (GammaData * data, gpointer dest, gpointer src)
{
  gint i;
  guint16 *s = src;
  guint8 *d = dest;
  guint8 *table = data->gamma_table;
  gint width = data->width * 4;

  for (i = 0; i < width; i++)
    d[i] = table[s[i]];
}

static void
gamma_convert_u16_u16 (GammaData * data, gpointer dest, gpointer src)
{
  gint i;
  guint16 *s = src;
  guint16 *d = dest;
  guint16 *table = data->gamma_table;
  gint width = data->width * 4;

  for (i = 0; i < width; i++)
    d[i] = table[s[i]];
}

static void
setup_gamma_decode (GstVideoConverter * convert)
{
  GstVideoTransferFunction func;
  guint16 *t;
  gint i;

  func = convert->in_info.colorimetry.transfer;

  convert->gamma_dec.width = convert->current_width;
  if (convert->current_bits == 8) {
    GST_DEBUG ("gamma decode 8->16: %d", func);
    convert->gamma_dec.gamma_func = gamma_convert_u8_u16;
    t = convert->gamma_dec.gamma_table = g_malloc (sizeof (guint16) * 256);

    for (i = 0; i < 256; i++)
      t[i] = rint (gst_video_color_transfer_decode (func, i / 255.0) * 65535.0);
  } else {
    GST_DEBUG ("gamma decode 16->16: %d", func);
    convert->gamma_dec.gamma_func = gamma_convert_u16_u16;
    t = convert->gamma_dec.gamma_table = g_malloc (sizeof (guint16) * 65536);

    for (i = 0; i < 65536; i++)
      t[i] =
          rint (gst_video_color_transfer_decode (func, i / 65535.0) * 65535.0);
  }
  convert->current_bits = 16;
  convert->current_pstride = 8;
  convert->current_format = GST_VIDEO_FORMAT_ARGB64;
}

static void
setup_gamma_encode (GstVideoConverter * convert, gint target_bits)
{
  GstVideoTransferFunction func;
  gint i;

  func = convert->out_info.colorimetry.transfer;

  convert->gamma_enc.width = convert->current_width;
  if (target_bits == 8) {
    guint8 *t;

    GST_DEBUG ("gamma encode 16->8: %d", func);
    convert->gamma_enc.gamma_func = gamma_convert_u16_u8;
    t = convert->gamma_enc.gamma_table = g_malloc (sizeof (guint8) * 65536);

    for (i = 0; i < 65536; i++)
      t[i] = rint (gst_video_color_transfer_encode (func, i / 65535.0) * 255.0);
  } else {
    guint16 *t;

    GST_DEBUG ("gamma encode 16->16: %d", func);
    convert->gamma_enc.gamma_func = gamma_convert_u16_u16;
    t = convert->gamma_enc.gamma_table = g_malloc (sizeof (guint16) * 65536);

    for (i = 0; i < 65536; i++)
      t[i] =
          rint (gst_video_color_transfer_encode (func, i / 65535.0) * 65535.0);
  }
}

static GstLineCache *
chain_convert_to_RGB (GstVideoConverter * convert, GstLineCache * prev)
{
  gboolean do_gamma;

  do_gamma = CHECK_GAMMA_REMAP (convert);

  if (do_gamma) {
    gint scale;

    if (!convert->unpack_rgb) {
      color_matrix_set_identity (&convert->to_RGB_matrix);
      compute_matrix_to_RGB (convert, &convert->to_RGB_matrix);

      /* matrix is in 0..1 range, scale to current bits */
      GST_DEBUG ("chain RGB convert");
      scale = 1 << convert->current_bits;
      color_matrix_scale_components (&convert->to_RGB_matrix,
          (float) scale, (float) scale, (float) scale);

      prepare_matrix (convert, &convert->to_RGB_matrix);

      if (convert->current_bits == 8)
        convert->current_format = GST_VIDEO_FORMAT_ARGB;
      else
        convert->current_format = GST_VIDEO_FORMAT_ARGB64;
    }

    prev = convert->to_RGB_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = FALSE;
    gst_line_cache_set_need_line_func (convert->to_RGB_lines,
        do_convert_to_RGB_lines, convert, NULL);

    GST_DEBUG ("chain gamma decode");
    setup_gamma_decode (convert);
  }
  return prev;
}

static GstLineCache *
chain_hscale (GstVideoConverter * convert, GstLineCache * prev)
{
  gint method;
  guint taps;

  prev = convert->hscale_lines = gst_line_cache_new (prev);
  prev->write_input = FALSE;
  prev->pass_alloc = FALSE;
  gst_line_cache_set_need_line_func (convert->hscale_lines,
      do_hscale_lines, convert, NULL);

  if (!gst_structure_get_enum (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, &method))
    method = GST_VIDEO_RESAMPLER_METHOD_CUBIC;
  if (!gst_structure_get_uint (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_TAPS, &taps))
    taps = 0;

  convert->h_scaler =
      gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
      convert->in_width, convert->out_width, convert->config);

  GST_DEBUG ("chain hscale %d->%d, taps %d, method %d",
      convert->in_width, convert->out_width, taps, method);

  convert->current_width = convert->out_width;
  convert->h_scale_format = convert->current_format;

  return prev;
}

static GstLineCache *
chain_vscale (GstVideoConverter * convert, GstLineCache * prev)
{
  GstVideoScalerFlags flags;
  gint method;
  guint taps;

  flags = GST_VIDEO_INFO_IS_INTERLACED (&convert->in_info) ?
      GST_VIDEO_SCALER_FLAG_INTERLACED : 0;

  if (!gst_structure_get_enum (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, &method))
    method = GST_VIDEO_RESAMPLER_METHOD_CUBIC;
  if (!gst_structure_get_uint (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_TAPS, &taps))
    taps = 0;

  convert->v_scaler =
      gst_video_scaler_new (method, flags, taps, convert->in_height,
      convert->out_height, convert->config);
  convert->v_scale_width = convert->current_width;
  convert->v_scale_format = convert->current_format;
  convert->current_height = convert->out_height;

  gst_video_scaler_get_coeff (convert->v_scaler, 0, NULL, &taps);

  GST_DEBUG ("chain vscale %d->%d, taps %d, method %d",
      convert->in_height, convert->out_height, taps, method);

  prev = convert->vscale_lines = gst_line_cache_new (prev);
  prev->pass_alloc = (taps == 1);
  prev->write_input = FALSE;
  gst_line_cache_set_need_line_func (convert->vscale_lines,
      do_vscale_lines, convert, NULL);

  return prev;
}

static GstLineCache *
chain_scale (GstVideoConverter * convert, GstLineCache * prev, gboolean force)
{
  gint s0, s1, s2, s3;

  s0 = convert->current_width * convert->current_height;
  s3 = convert->out_width * convert->out_height;

  GST_DEBUG ("%d <> %d", s0, s3);

  if (s3 <= s0 || force) {
    /* we are making the image smaller or are forced to resample */
    s1 = convert->out_width * convert->current_height;
    s2 = convert->current_width * convert->out_height;

    GST_DEBUG ("%d <> %d", s1, s2);

    if (s1 <= s2) {
      /* h scaling first produces less pixels */
      if (convert->current_width != convert->out_width)
        prev = chain_hscale (convert, prev);
      if (convert->current_height != convert->out_height)
        prev = chain_vscale (convert, prev);
    } else {
      /* v scaling first produces less pixels */
      if (convert->current_height != convert->out_height)
        prev = chain_vscale (convert, prev);
      if (convert->current_width != convert->out_width)
        prev = chain_hscale (convert, prev);
    }
  }
  return prev;
}

static GstLineCache *
chain_convert (GstVideoConverter * convert, GstLineCache * prev)
{
  gboolean do_gamma, do_conversion, pass_alloc = FALSE;
  gboolean same_matrix, same_primaries, same_bits;
  MatrixData p1, p2;

  same_bits = convert->unpack_bits == convert->pack_bits;
  same_matrix =
      convert->in_info.colorimetry.matrix ==
      convert->out_info.colorimetry.matrix;
  if (CHECK_PRIMARIES_NONE (convert)) {
    same_primaries = TRUE;
  } else {
    same_primaries =
        convert->in_info.colorimetry.primaries ==
        convert->out_info.colorimetry.primaries;
  }

  GST_DEBUG ("matrix %d -> %d (%d)", convert->in_info.colorimetry.matrix,
      convert->out_info.colorimetry.matrix, same_matrix);
  GST_DEBUG ("bits %d -> %d (%d)", convert->unpack_bits, convert->pack_bits,
      same_bits);
  GST_DEBUG ("primaries %d -> %d (%d)", convert->in_info.colorimetry.primaries,
      convert->out_info.colorimetry.primaries, same_primaries);

  color_matrix_set_identity (&convert->convert_matrix);

  if (!same_primaries) {
    const GstVideoColorPrimariesInfo *pi;

    pi = gst_video_color_primaries_get_info (convert->in_info.
        colorimetry.primaries);
    color_matrix_RGB_to_XYZ (&p1, pi->Rx, pi->Ry, pi->Gx, pi->Gy, pi->Bx,
        pi->By, pi->Wx, pi->Wy);
    GST_DEBUG ("to XYZ matrix");
    color_matrix_debug (&p1);
    GST_DEBUG ("current matrix");
    color_matrix_multiply (&convert->convert_matrix, &convert->convert_matrix,
        &p1);
    color_matrix_debug (&convert->convert_matrix);

    pi = gst_video_color_primaries_get_info (convert->out_info.
        colorimetry.primaries);
    color_matrix_RGB_to_XYZ (&p2, pi->Rx, pi->Ry, pi->Gx, pi->Gy, pi->Bx,
        pi->By, pi->Wx, pi->Wy);
    color_matrix_invert (&p2, &p2);
    GST_DEBUG ("to RGB matrix");
    color_matrix_debug (&p2);
    color_matrix_multiply (&convert->convert_matrix, &convert->convert_matrix,
        &p2);
    GST_DEBUG ("current matrix");
    color_matrix_debug (&convert->convert_matrix);
  }

  do_gamma = CHECK_GAMMA_REMAP (convert);
  if (!do_gamma) {

    convert->in_bits = convert->unpack_bits;
    convert->out_bits = convert->pack_bits;

    if (!same_bits || !same_matrix || !same_primaries) {
      /* no gamma, combine all conversions into 1 */
      if (convert->in_bits < convert->out_bits) {
        gint scale = 1 << (convert->out_bits - convert->in_bits);
        color_matrix_scale_components (&convert->convert_matrix,
            1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
      }
      GST_DEBUG ("to RGB matrix");
      compute_matrix_to_RGB (convert, &convert->convert_matrix);
      GST_DEBUG ("current matrix");
      color_matrix_debug (&convert->convert_matrix);

      GST_DEBUG ("to YUV matrix");
      compute_matrix_to_YUV (convert, &convert->convert_matrix);
      GST_DEBUG ("current matrix");
      color_matrix_debug (&convert->convert_matrix);
      if (convert->in_bits > convert->out_bits) {
        gint scale = 1 << (convert->in_bits - convert->out_bits);
        color_matrix_scale_components (&convert->convert_matrix,
            (float) scale, (float) scale, (float) scale);
      }
      convert->current_bits = MAX (convert->in_bits, convert->out_bits);

      do_conversion = TRUE;
      if (!same_matrix || !same_primaries)
        prepare_matrix (convert, &convert->convert_matrix);
      if (convert->in_bits == convert->out_bits)
        pass_alloc = TRUE;
    } else
      do_conversion = FALSE;
  } else {
    /* we did gamma, just do colorspace conversion if needed */
    if (same_primaries) {
      do_conversion = FALSE;
    } else {
      prepare_matrix (convert, &convert->convert_matrix);
      convert->in_bits = convert->out_bits = 16;
      pass_alloc = TRUE;
      do_conversion = TRUE;
    }
  }

  if (do_conversion) {
    GST_DEBUG ("chain conversion");
    prev = convert->convert_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = pass_alloc;
    gst_line_cache_set_need_line_func (convert->convert_lines,
        do_convert_lines, convert, NULL);
  }
  return prev;
}

static GstLineCache *
chain_convert_to_YUV (GstVideoConverter * convert, GstLineCache * prev)
{
  gboolean do_gamma;

  do_gamma = CHECK_GAMMA_REMAP (convert);

  if (do_gamma) {
    gint scale;

    setup_gamma_encode (convert, convert->pack_bits);

    convert->current_bits = convert->pack_bits;
    convert->current_pstride = convert->current_bits >> 1;
    GST_DEBUG ("chain gamma encode");

    if (!convert->pack_rgb) {
      color_matrix_set_identity (&convert->to_YUV_matrix);
      compute_matrix_to_YUV (convert, &convert->to_YUV_matrix);

      /* matrix is in 0..255 range, scale to pack bits */
      GST_DEBUG ("chain YUV convert");
      scale = 1 << convert->pack_bits;
      color_matrix_scale_components (&convert->to_YUV_matrix,
          1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
      prepare_matrix (convert, &convert->to_YUV_matrix);
    }

    prev = convert->to_YUV_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = (do_gamma == FALSE);
    gst_line_cache_set_need_line_func (convert->to_YUV_lines,
        do_convert_to_YUV_lines, convert, NULL);

    convert->current_format = convert->pack_format;
    convert->current_pstride = convert->current_bits >> 1;
  }

  return prev;
}

static GstLineCache *
chain_downsample (GstVideoConverter * convert, GstLineCache * prev)
{
  if (convert->downsample) {
    GST_DEBUG ("chain downsample");
    prev = convert->downsample_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = TRUE;
    gst_line_cache_set_need_line_func (convert->downsample_lines,
        do_downsample_lines, convert, NULL);
  }
  return prev;
}

static GstLineCache *
chain_pack (GstVideoConverter * convert, GstLineCache * prev)
{
  convert->pack_nlines = convert->out_info.finfo->pack_lines;
  convert->pack_pstride = convert->current_pstride;
  convert->identity_pack =
      (convert->out_info.finfo->format ==
      convert->out_info.finfo->unpack_format);
  GST_DEBUG ("chain pack line format %s, pstride %d, identity_pack %d (%d %d)",
      gst_video_format_to_string (convert->current_format),
      convert->current_pstride, convert->identity_pack,
      convert->out_info.finfo->format, convert->out_info.finfo->unpack_format);

  return prev;
}

static gint
get_opt_int (GstVideoConverter * convert, const gchar * opt, gint def)
{
  gint res;
  if (!gst_structure_get_int (convert->config, opt, &res))
    res = def;
  return res;
}

static guint
get_opt_uint (GstVideoConverter * convert, const gchar * opt, guint def)
{
  guint res;
  if (!gst_structure_get_uint (convert->config, opt, &res))
    res = def;
  return res;
}

static gboolean
get_opt_bool (GstVideoConverter * convert, const gchar * opt, gboolean def)
{
  gboolean res;
  if (!gst_structure_get_boolean (convert->config, opt, &res))
    res = def;
  return res;
}



static void
setup_allocators (GstVideoConverter * convert)
{
  GstLineCache *cache;
  GstLineCacheAllocLineFunc alloc_line;
  gboolean alloc_writable;

  /* start with using dest lines if we can directly write into it */
  if (convert->identity_pack) {
    alloc_line = get_dest_line;
    alloc_writable = TRUE;
  } else {
    alloc_line = get_border_temp_line;
    alloc_writable = FALSE;
  }

  /* now walk backwards, we try to write into the dest lines directly
   * and keep track if the source needs to be writable */
  for (cache = convert->pack_lines; cache; cache = cache->prev) {
    gst_line_cache_set_alloc_line_func (cache, alloc_line, convert, NULL);
    cache->alloc_writable = alloc_writable;

    if (cache->pass_alloc == FALSE) {
      /* can't pass allocator, use temp lines */
      alloc_line = get_temp_line;
      alloc_writable = FALSE;
    }
    /* if someone writes to the input, we need a writable line from the
     * previous cache */
    if (cache->write_input)
      alloc_writable = TRUE;
  }
}

/**
 * gst_video_converter_new:
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @config: a #GstStructure with configuration options
 *
 * Create a new converter object to convert between @in_info and @out_info
 * with @config.
 *
 * Returns: a #GstVideoConverter or %NULL if conversion is not possible.
 *
 * Since: 1.6
 */
GstVideoConverter *
gst_video_converter_new (GstVideoInfo * in_info, GstVideoInfo * out_info,
    GstStructure * config)
{
  GstVideoConverter *convert;
  gint width;
  GstLineCache *prev;
  const GstVideoFormatInfo *fin, *fout, *finfo;

  g_return_val_if_fail (in_info != NULL, NULL);
  g_return_val_if_fail (out_info != NULL, NULL);
  /* we won't ever do framerate conversion */
  g_return_val_if_fail (in_info->fps_n == out_info->fps_n, NULL);
  g_return_val_if_fail (in_info->fps_d == out_info->fps_d, NULL);
  /* we won't ever do deinterlace */
  g_return_val_if_fail (in_info->interlace_mode == out_info->interlace_mode,
      NULL);

  convert = g_slice_new0 (GstVideoConverter);

  fin = in_info->finfo;
  fout = out_info->finfo;

  convert->in_info = *in_info;
  convert->out_info = *out_info;

  /* default config */
  convert->config = gst_structure_new ("GstVideoConverter",
      GST_VIDEO_CONVERTER_OPT_DITHER_METHOD, GST_TYPE_VIDEO_DITHER_METHOD,
      GST_VIDEO_DITHER_NONE, NULL);
  if (config)
    gst_video_converter_set_config (convert, config);

  convert->in_maxwidth = GST_VIDEO_INFO_WIDTH (in_info);
  convert->in_maxheight = GST_VIDEO_INFO_HEIGHT (in_info);
  convert->out_maxwidth = GST_VIDEO_INFO_WIDTH (out_info);
  convert->out_maxheight = GST_VIDEO_INFO_HEIGHT (out_info);

  convert->in_x = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_SRC_X, 0);
  convert->in_y = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_SRC_Y, 0);
  convert->in_width = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_SRC_WIDTH, convert->in_maxwidth);
  convert->in_height = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_SRC_HEIGHT, convert->in_maxheight);

  convert->in_x &= ~((1 << fin->w_sub[1]) - 1);
  convert->in_y &= ~((1 << fin->h_sub[1]) - 1);

  convert->out_x = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_DEST_X, 0);
  convert->out_y = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_DEST_Y, 0);
  convert->out_width = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_DEST_WIDTH, convert->out_maxwidth);
  convert->out_height = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_DEST_HEIGHT, convert->out_maxheight);

  convert->out_x &= ~((1 << fout->w_sub[1]) - 1);
  convert->out_y &= ~((1 << fout->h_sub[1]) - 1);

  convert->fill_border = get_opt_bool (convert,
      GST_VIDEO_CONVERTER_OPT_FILL_BORDER, TRUE);
  convert->border_argb = get_opt_uint (convert,
      GST_VIDEO_CONVERTER_OPT_BORDER_ARGB, 0x00000000);

  convert->unpack_format = in_info->finfo->unpack_format;
  finfo = gst_video_format_get_info (convert->unpack_format);
  convert->unpack_bits = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, 0);
  convert->unpack_rgb = GST_VIDEO_FORMAT_INFO_IS_RGB (finfo);

  convert->pack_format = out_info->finfo->unpack_format;
  finfo = gst_video_format_get_info (convert->pack_format);
  convert->pack_bits = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, 0);
  convert->pack_rgb = GST_VIDEO_FORMAT_INFO_IS_RGB (finfo);

  if (video_converter_lookup_fastpath (convert))
    goto done;

  if (in_info->finfo->unpack_func == NULL)
    goto no_unpack_func;

  if (out_info->finfo->pack_func == NULL)
    goto no_pack_func;

  convert->convert = video_converter_generic;

  convert->current_format = GST_VIDEO_INFO_FORMAT (in_info);
  convert->current_width = convert->in_width;
  convert->current_height = convert->in_height;

  /* unpack */
  prev = chain_unpack_line (convert);
  /* upsample chroma */
  prev = chain_upsample (convert, prev);
  /* convert to gamma decoded RGB */
  prev = chain_convert_to_RGB (convert, prev);
  /* do all downscaling */
  prev = chain_scale (convert, prev, FALSE);
  /* do conversion between color spaces */
  prev = chain_convert (convert, prev);
  /* do all remaining (up)scaling */
  prev = chain_scale (convert, prev, TRUE);
  /* convert to gamma encoded Y'Cb'Cr' */
  prev = chain_convert_to_YUV (convert, prev);
  /* downsample chroma */
  prev = chain_downsample (convert, prev);
  /* pack into final format */
  convert->pack_lines = chain_pack (convert, prev);

  /* now figure out allocators */
  setup_allocators (convert);

  width = MAX (convert->in_maxwidth, convert->out_maxwidth);
  width += convert->out_x;
  convert->errline = g_malloc0 (sizeof (guint16) * width * 4);

  if (convert->fill_border && (convert->out_height < convert->out_maxheight ||
          convert->out_width < convert->out_maxwidth)) {
    guint32 border_val;

    convert->borderline = g_malloc0 (sizeof (guint16) * width * 4);

    if (GST_VIDEO_INFO_IS_YUV (&convert->out_info)) {
      /* FIXME, convert to AYUV, just black for now */
      border_val = GINT32_FROM_BE (0x00007f7f);
    } else {
      border_val = GINT32_FROM_BE (convert->border_argb);
    }
    if (convert->pack_bits == 8)
      video_orc_splat_u32 (convert->borderline, border_val, width);
    else
      video_orc_splat_u64 (convert->borderline, border_val, width);
  } else {
    convert->borderline = NULL;
  }

  /* FIXME */
  alloc_tmplines (convert, 64, 4, width);

done:
  return convert;

  /* ERRORS */
no_unpack_func:
  {
    GST_ERROR ("no unpack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
    gst_video_converter_free (convert);
    return NULL;
  }
no_pack_func:
  {
    GST_ERROR ("no pack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    gst_video_converter_free (convert);
    return NULL;
  }
}

/**
 * gst_video_converter_free:
 * @convert: a #GstVideoConverter
 *
 * Free @convert
 *
 * Since: 1.6
 */
void
gst_video_converter_free (GstVideoConverter * convert)
{
  gint i;

  g_return_if_fail (convert != NULL);

  if (convert->upsample)
    gst_video_chroma_resample_free (convert->upsample);
  if (convert->downsample)
    gst_video_chroma_resample_free (convert->downsample);
  if (convert->v_scaler)
    gst_video_scaler_free (convert->v_scaler);
  if (convert->h_scaler)
    gst_video_scaler_free (convert->h_scaler);

  if (convert->unpack_lines)
    gst_line_cache_free (convert->unpack_lines);
  if (convert->upsample_lines)
    gst_line_cache_free (convert->upsample_lines);
  if (convert->to_RGB_lines)
    gst_line_cache_free (convert->to_RGB_lines);
  if (convert->hscale_lines)
    gst_line_cache_free (convert->hscale_lines);
  if (convert->vscale_lines)
    gst_line_cache_free (convert->vscale_lines);
  if (convert->convert_lines)
    gst_line_cache_free (convert->convert_lines);
  if (convert->to_YUV_lines)
    gst_line_cache_free (convert->to_YUV_lines);
  if (convert->downsample_lines)
    gst_line_cache_free (convert->downsample_lines);

  g_free (convert->gamma_dec.gamma_table);
  g_free (convert->gamma_enc.gamma_table);

  for (i = 0; i < convert->n_tmplines; i++)
    g_free (convert->tmplines[i]);
  g_free (convert->tmplines);
  for (i = 0; i < convert->n_btmplines; i++)
    g_free (convert->btmplines[i]);
  g_free (convert->btmplines);
  g_free (convert->errline);
  g_free (convert->borderline);

  if (convert->config)
    gst_structure_free (convert->config);

  g_slice_free (GstVideoConverter, convert);
}

static void
video_dither_verterr (GstVideoConverter * convert, guint16 * pixels, int j)
{
  int i;
  guint16 *errline = convert->errline;
  unsigned int mask = 0xff;

  for (i = 0; i < 4 * convert->in_width; i++) {
    int x = pixels[i] + errline[i];
    if (x > 65535)
      x = 65535;
    pixels[i] = x;
    errline[i] = x & mask;
  }
}

static void
video_dither_halftone (GstVideoConverter * convert, guint16 * pixels, int j)
{
  int i;
  static guint16 halftone[8][8] = {
    {0, 128, 32, 160, 8, 136, 40, 168},
    {192, 64, 224, 96, 200, 72, 232, 104},
    {48, 176, 16, 144, 56, 184, 24, 152},
    {240, 112, 208, 80, 248, 120, 216, 88},
    {12, 240, 44, 172, 4, 132, 36, 164},
    {204, 76, 236, 108, 196, 68, 228, 100},
    {60, 188, 28, 156, 52, 180, 20, 148},
    {252, 142, 220, 92, 244, 116, 212, 84}
  };

  for (i = 0; i < convert->in_width * 4; i++) {
    int x;
    x = pixels[i] + halftone[(i >> 2) & 7][j & 7];
    if (x > 65535)
      x = 65535;
    pixels[i] = x;
  }
}

static gboolean
copy_config (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstVideoConverter *convert = user_data;

  gst_structure_id_set_value (convert->config, field_id, value);

  return TRUE;
}

/**
 * gst_video_converter_set_config:
 * @convert: a #GstVideoConverter
 * @config: (transfer full): a #GstStructure
 *
 * Set @config as extra configuraion for @convert.
 *
 * If the parameters in @config can not be set exactly, this function returns
 * %FALSE and will try to update as much state as possible. The new state can
 * then be retrieved and refined with gst_video_converter_get_config().
 *
 * Look at the #GST_VIDEO_CONVERTER_OPT_* fields to check valid configuration
 * option and values.
 *
 * Returns: %TRUE when @config could be set.
 *
 * Since: 1.6
 */
gboolean
gst_video_converter_set_config (GstVideoConverter * convert,
    GstStructure * config)
{
  gint dither;
  gboolean res = TRUE;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  if (gst_structure_get_enum (config, GST_VIDEO_CONVERTER_OPT_DITHER_METHOD,
          GST_TYPE_VIDEO_DITHER_METHOD, &dither)) {
    gboolean update = TRUE;

    switch (dither) {
      case GST_VIDEO_DITHER_NONE:
        convert->dither16 = NULL;
        break;
      case GST_VIDEO_DITHER_VERTERR:
        convert->dither16 = video_dither_verterr;
        break;
      case GST_VIDEO_DITHER_HALFTONE:
        convert->dither16 = video_dither_halftone;
        break;
      default:
        update = FALSE;
        break;
    }
    if (update)
      gst_structure_set (convert->config, GST_VIDEO_CONVERTER_OPT_DITHER_METHOD,
          GST_TYPE_VIDEO_DITHER_METHOD, dither, NULL);
    else
      res = FALSE;
  }
  if (res)
    gst_structure_foreach (config, copy_config, convert);

  gst_structure_free (config);

  return res;
}

/**
 * gst_video_converter_get_config:
 * @@convert: a #GstVideoConverter
 *
 * Get the current configuration of @convert.
 *
 * Returns: a #GstStructure that remains valid for as long as @convert is valid
 *   or until gst_video_converter_set_config() is called.
 */
const GstStructure *
gst_video_converter_get_config (GstVideoConverter * convert)
{
  g_return_val_if_fail (convert != NULL, NULL);

  return convert->config;
}

/**
 * gst_video_converter_frame:
 * @convert: a #GstVideoConverter
 * @dest: a #GstVideoFrame
 * @src: a #GstVideoFrame
 *
 * Convert the pixels of @src into @dest using @convert.
 *
 * Since: 1.6
 */
void
gst_video_converter_frame (GstVideoConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest)
{
  g_return_if_fail (convert != NULL);
  g_return_if_fail (src != NULL);
  g_return_if_fail (dest != NULL);

  convert->convert (convert, src, dest);
}

static void
video_converter_compute_matrix (GstVideoConverter * convert)
{
  MatrixData *dst = &convert->convert_matrix;

  color_matrix_set_identity (dst);
  compute_matrix_to_RGB (convert, dst);
  compute_matrix_to_YUV (convert, dst);

  convert->current_bits = 8;
  prepare_matrix (convert, dst);
}

static void
video_converter_compute_resample (GstVideoConverter * convert)
{
  GstVideoInfo *in_info, *out_info;
  const GstVideoFormatInfo *sfinfo, *dfinfo;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  sfinfo = in_info->finfo;
  dfinfo = out_info->finfo;

  if (sfinfo->w_sub[2] != dfinfo->w_sub[2] ||
      sfinfo->h_sub[2] != dfinfo->h_sub[2] ||
      in_info->chroma_site != out_info->chroma_site ||
      in_info->width != out_info->width ||
      in_info->height != out_info->height) {
    GstVideoChromaFlags flags = (GST_VIDEO_INFO_IS_INTERLACED (in_info) ?
        GST_VIDEO_CHROMA_FLAG_INTERLACED : 0);

    convert->upsample = gst_video_chroma_resample_new (0,
        in_info->chroma_site, flags, sfinfo->unpack_format, sfinfo->w_sub[2],
        sfinfo->h_sub[2]);

    convert->downsample = gst_video_chroma_resample_new (0,
        out_info->chroma_site, flags, dfinfo->unpack_format, -dfinfo->w_sub[2],
        -dfinfo->h_sub[2]);

  } else {
    convert->upsample = NULL;
    convert->downsample = NULL;
  }

  if (convert->upsample) {
    gst_video_chroma_resample_get_info (convert->upsample,
        &convert->up_n_lines, &convert->up_offset);
  } else {
    convert->up_n_lines = 1;
    convert->up_offset = 0;
  }
  if (convert->downsample) {
    gst_video_chroma_resample_get_info (convert->downsample,
        &convert->down_n_lines, &convert->down_offset);
  } else {
    convert->down_n_lines = 1;
    convert->down_offset = 0;
  }
  GST_DEBUG ("upsample: %p, site: %d, offset %d, n_lines %d", convert->upsample,
      in_info->chroma_site, convert->up_offset, convert->up_n_lines);
  GST_DEBUG ("downsample: %p, site: %d, offset %d, n_lines %d",
      convert->downsample, out_info->chroma_site, convert->down_offset,
      convert->down_n_lines);
}

#define FRAME_GET_PLANE_STRIDE(frame, plane) \
  GST_VIDEO_FRAME_PLANE_STRIDE (frame, plane)
#define FRAME_GET_PLANE_LINE(frame, plane, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_PLANE_DATA (frame, plane))) + \
      FRAME_GET_PLANE_STRIDE (frame, plane) * (line))

#define FRAME_GET_COMP_STRIDE(frame, comp) \
  GST_VIDEO_FRAME_COMP_STRIDE (frame, comp)
#define FRAME_GET_COMP_LINE(frame, comp, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_COMP_DATA (frame, comp))) + \
      FRAME_GET_COMP_STRIDE (frame, comp) * (line))

#define FRAME_GET_STRIDE(frame)      FRAME_GET_PLANE_STRIDE (frame, 0)
#define FRAME_GET_LINE(frame,line)   FRAME_GET_PLANE_LINE (frame, 0, line)

#define FRAME_GET_Y_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_Y, line)
#define FRAME_GET_U_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_U, line)
#define FRAME_GET_V_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_V, line)
#define FRAME_GET_A_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_A, line)

#define FRAME_GET_Y_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_Y)
#define FRAME_GET_U_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_U)
#define FRAME_GET_V_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_V)
#define FRAME_GET_A_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_A)


#define UNPACK_FRAME(frame,dest,line,x,width)        \
  frame->info.finfo->unpack_func (frame->info.finfo, \
      (GST_VIDEO_FRAME_IS_INTERLACED (frame) ?       \
        GST_VIDEO_PACK_FLAG_INTERLACED :             \
        GST_VIDEO_PACK_FLAG_NONE),                   \
      dest, frame->data, frame->info.stride, x,      \
      line, width)
#define PACK_FRAME(frame,src,line,width)             \
  frame->info.finfo->pack_func (frame->info.finfo,   \
      (GST_VIDEO_FRAME_IS_INTERLACED (frame) ?       \
        GST_VIDEO_PACK_FLAG_INTERLACED :             \
        GST_VIDEO_PACK_FLAG_NONE),                   \
      src, 0, frame->data, frame->info.stride,       \
      frame->info.chroma_site, line, width);


static gpointer
get_dest_line (GstLineCache * cache, gint idx, gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  guint8 *line;
  gint pstride = convert->pack_pstride;
  gint out_x = convert->out_x;
  guint cline;

  cline = CLAMP (idx, 0, convert->out_maxheight - 1);

  GST_DEBUG ("get dest line %d", cline);
  line = FRAME_GET_LINE (convert->dest, cline);

  if (convert->borderline) {
    gint r_border = (out_x + convert->out_width) * pstride;
    gint rb_width = convert->out_maxwidth * pstride - r_border;
    gint lb_width = out_x * pstride;

    memcpy (line, convert->borderline, lb_width);
    memcpy (line + r_border, convert->borderline, rb_width);
  }
  line += out_x * pstride;

  return line;
}

static gboolean
do_unpack_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer tmpline;
  guint cline;

  cline = CLAMP (in_line + convert->in_y, 0, convert->in_maxheight - 1);

  if (cache->alloc_writable || !convert->identity_unpack) {
    tmpline = gst_line_cache_alloc_line (cache, out_line);
    GST_DEBUG ("unpack line %d (%u) %p", in_line, cline, tmpline);
    UNPACK_FRAME (convert->src, tmpline, cline, convert->in_x,
        convert->in_width);
  } else {
    GST_DEBUG ("get src line %d (%u)", in_line, cline);
    tmpline = ((guint8 *) FRAME_GET_LINE (convert->src, cline)) +
        convert->in_x * convert->unpack_pstride;
  }
  gst_line_cache_add_line (cache, in_line, tmpline);

  return TRUE;
}

static gboolean
do_upsample_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines;
  gint i, start_line, n_lines;

  n_lines = convert->up_n_lines;
  start_line = in_line;
  if (start_line < n_lines + convert->up_offset)
    start_line += convert->up_offset;

  /* get the lines needed for chroma upsample */
  lines = gst_line_cache_get_lines (cache->prev, out_line, start_line, n_lines);

  GST_DEBUG ("doing upsample %d-%d", start_line, start_line + n_lines - 1);
  gst_video_chroma_resample (convert->upsample, lines, convert->in_width);

  for (i = 0; i < n_lines; i++)
    gst_line_cache_add_line (cache, start_line + i, lines[i]);

  return TRUE;
}

static gboolean
do_convert_to_RGB_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  MatrixData *data = &convert->to_RGB_matrix;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);
  destline = lines[0];

  if (data->matrix_func) {
    GST_DEBUG ("to RGB line %d", in_line);
    data->matrix_func (data, destline);
  }
  if (convert->gamma_dec.gamma_func) {
    destline = gst_line_cache_alloc_line (cache, out_line);

    GST_DEBUG ("gamma decode line %d", in_line);
    convert->gamma_dec.gamma_func (&convert->gamma_dec, destline, lines[0]);
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_hscale_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);

  destline = gst_line_cache_alloc_line (cache, out_line);

  GST_DEBUG ("hresample line %d", in_line);
  gst_video_scaler_horizontal (convert->h_scaler, convert->h_scale_format,
      lines[0], destline, 0, convert->out_width);

  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_vscale_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines, destline;
  guint sline, n_lines;
  guint cline;

  cline = CLAMP (in_line, 0, convert->out_height - 1);

  gst_video_scaler_get_coeff (convert->v_scaler, cline, &sline, &n_lines);
  lines = gst_line_cache_get_lines (cache->prev, out_line, sline, n_lines);

  destline = gst_line_cache_alloc_line (cache, out_line);

  GST_DEBUG ("vresample line %d %d-%d", in_line, sline, sline + n_lines - 1);
  gst_video_scaler_vertical (convert->v_scaler, convert->v_scale_format,
      lines, destline, cline, convert->v_scale_width);

  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_convert_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  MatrixData *data = &convert->convert_matrix;
  gpointer *lines, destline;
  guint in_bits, out_bits;
  gint width;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);

  destline = lines[0];

  in_bits = convert->in_bits;
  out_bits = convert->out_bits;

  width = MIN (convert->in_width, convert->out_width);

  if (out_bits == 16 || in_bits == 16) {
    gpointer srcline = lines[0];

    if (out_bits != in_bits)
      destline = gst_line_cache_alloc_line (cache, out_line);

    /* FIXME, we can scale in the conversion matrix */
    if (in_bits == 8) {
      GST_DEBUG ("8->16 line %d", in_line);
      video_orc_convert_u8_to_u16 (destline, srcline, width * 4);
      srcline = destline;
    }

    if (data->matrix_func) {
      GST_DEBUG ("matrix line %d", in_line);
      data->matrix_func (data, srcline);
    }
    if (convert->dither16)
      convert->dither16 (convert, srcline, in_line);

    if (out_bits == 8) {
      GST_DEBUG ("16->8 line %d", in_line);
      video_orc_convert_u16_to_u8 (destline, srcline, width * 4);
    }
  } else {
    if (data->matrix_func) {
      GST_DEBUG ("matrix line %d", in_line);
      data->matrix_func (data, destline);
    }
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_convert_to_YUV_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  MatrixData *data = &convert->to_YUV_matrix;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);
  destline = lines[0];

  if (convert->gamma_enc.gamma_func) {
    destline = gst_line_cache_alloc_line (cache, out_line);

    GST_DEBUG ("gamma encode line %d", in_line);
    convert->gamma_enc.gamma_func (&convert->gamma_enc, destline, lines[0]);
  }
  if (data->matrix_func) {
    GST_DEBUG ("to YUV line %d", in_line);
    data->matrix_func (data, destline);
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_downsample_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines;
  gint i, start_line, n_lines;

  n_lines = convert->down_n_lines;
  start_line = in_line;
  if (start_line < n_lines + convert->down_offset)
    start_line += convert->down_offset;

  /* get the lines needed for chroma downsample */
  lines = gst_line_cache_get_lines (cache->prev, out_line, start_line, n_lines);

  GST_DEBUG ("downsample line %d %d-%d", in_line, start_line,
      start_line + n_lines - 1);
  gst_video_chroma_resample (convert->downsample, lines, convert->out_width);

  for (i = 0; i < n_lines; i++)
    gst_line_cache_add_line (cache, start_line + i, lines[i]);

  return TRUE;
}

static void
video_converter_generic (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint i;
  gint out_maxwidth, out_maxheight;
  gint out_x, out_y, out_height;
  gint pack_lines, pstride;
  gint lb_width;

  out_height = convert->out_height;
  out_maxwidth = convert->out_maxwidth;
  out_maxheight = convert->out_maxheight;

  out_x = convert->out_x;
  out_y = convert->out_y;

  convert->src = src;
  convert->dest = dest;

  pack_lines = convert->pack_nlines;    /* only 1 for now */
  pstride = convert->pack_pstride;

  lb_width = out_x * pstride;

  if (convert->borderline) {
    /* FIXME we should try to avoid PACK_FRAME */
    for (i = 0; i < out_y; i++)
      PACK_FRAME (dest, convert->borderline, i, out_maxwidth);
  }

  for (i = 0; i < out_height; i += pack_lines) {
    gpointer *lines;

    /* load the lines needed to pack */
    lines = gst_line_cache_get_lines (convert->pack_lines, i + out_y,
        i, pack_lines);

    if (!convert->identity_pack) {
      /* take away the border */
      guint8 *l = ((guint8 *) lines[0]) - lb_width;
      /* and pack into destination */
      GST_DEBUG ("pack line %d", i + out_y);
      PACK_FRAME (dest, l, i + out_y, out_maxwidth);
    }
  }

  if (convert->borderline) {
    for (i = out_y + out_height; i < out_maxheight; i++)
      PACK_FRAME (dest, convert->borderline, i, out_maxwidth);
  }
}

/* Fast paths */

#define GET_LINE_OFFSETS(interlaced,line,l1,l2) \
    if (interlaced) {                           \
      l1 = (line & 2 ? line - 1 : line);        \
      l2 = l1 + 2;                              \
    } else {                                    \
      l1 = line;                                \
      l2 = l1 + 1;                              \
    }

static void
convert_I420_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_I420_YUY2 (FRAME_GET_LINE (dest, l1),
        FRAME_GET_LINE (dest, l2),
        FRAME_GET_Y_LINE (src, l1),
        FRAME_GET_Y_LINE (src, l2),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_I420_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_I420_UYVY (FRAME_GET_LINE (dest, l1),
        FRAME_GET_LINE (dest, l2),
        FRAME_GET_Y_LINE (src, l1),
        FRAME_GET_Y_LINE (src, l2),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_I420_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_I420_AYUV (FRAME_GET_LINE (dest, l1),
        FRAME_GET_LINE (dest, l2),
        FRAME_GET_Y_LINE (src, l1),
        FRAME_GET_Y_LINE (src, l2),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1), width);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_I420_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_420_422 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  video_orc_planar_chroma_420_422 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);
}

static void
convert_I420_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_420_444 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  video_orc_planar_chroma_420_444 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_YUY2_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_YUY2_I420 (FRAME_GET_Y_LINE (dest, l1),
        FRAME_GET_Y_LINE (dest, l2),
        FRAME_GET_U_LINE (dest, i >> 1),
        FRAME_GET_V_LINE (dest, i >> 1),
        FRAME_GET_LINE (src, l1), FRAME_GET_LINE (src, l2), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_YUY2_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_YUY2_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_YUY2_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_YUY2_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_YUY2_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_YUY2_Y444 (FRAME_GET_COMP_LINE (dest, 0, 0),
      FRAME_GET_COMP_STRIDE (dest, 0), FRAME_GET_COMP_LINE (dest, 1, 0),
      FRAME_GET_COMP_STRIDE (dest, 1), FRAME_GET_COMP_LINE (dest, 2, 0),
      FRAME_GET_COMP_STRIDE (dest, 2), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}


static void
convert_UYVY_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_UYVY_I420 (FRAME_GET_COMP_LINE (dest, 0, l1),
        FRAME_GET_COMP_LINE (dest, 0, l2),
        FRAME_GET_COMP_LINE (dest, 1, i >> 1),
        FRAME_GET_COMP_LINE (dest, 2, i >> 1),
        FRAME_GET_LINE (src, l1), FRAME_GET_LINE (src, l2), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_UYVY_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_AYUV_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width/height */
  video_orc_convert_AYUV_I420 (FRAME_GET_Y_LINE (dest, 0),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (dest, 1),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      2 * FRAME_GET_STRIDE (src), FRAME_GET_LINE (src, 1),
      2 * FRAME_GET_STRIDE (src), width / 2, height / 2);
}

static void
convert_AYUV_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width */
  video_orc_convert_AYUV_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width */
  video_orc_convert_AYUV_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only works for even width */
  video_orc_convert_AYUV_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_Y42B_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_422_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  video_orc_planar_chroma_422_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_Y42B_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_422_444 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height);

  video_orc_planar_chroma_422_444 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y42B_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y42B_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width */
  video_orc_convert_Y42B_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_444_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), width / 2, height / 2);

  video_orc_planar_chroma_444_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), width / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
  }
}

static void
convert_Y444_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_444_422 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), width / 2, height);

  video_orc_planar_chroma_444_422 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y444_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y444_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y444_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width, height);
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static void
convert_AYUV_ARGB (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  MatrixData *data = &convert->convert_matrix;
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_ARGB (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_AYUV_BGRA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  video_orc_convert_AYUV_BGRA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_AYUV_ABGR (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  video_orc_convert_AYUV_ABGR (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_AYUV_RGBA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  video_orc_convert_AYUV_RGBA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_I420_BGRA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  for (i = 0; i < height; i++) {
    video_orc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1),
        data->im[0][0], data->im[0][2],
        data->im[2][1], data->im[1][1], data->im[1][2], width);
  }
}
#endif


/* Fast paths */

typedef struct
{
  GstVideoFormat in_format;
  GstVideoFormat out_format;
  gboolean keeps_interlaced;
  gboolean needs_color_matrix;
  gint width_align, height_align;
  void (*convert) (GstVideoConverter * convert, const GstVideoFrame * src,
      GstVideoFrame * dest);
} VideoTransform;

static const VideoTransform transforms[] = {
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, 0, 0,
      convert_I420_YUY2},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, 0, 0,
      convert_I420_UYVY},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, 0, 0,
      convert_I420_AYUV},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_Y42B, FALSE, FALSE, 0, 0,
      convert_I420_Y42B},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_Y444, FALSE, FALSE, 0, 0,
      convert_I420_Y444},

  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, 0, 0,
      convert_I420_YUY2},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, 0, 0,
      convert_I420_UYVY},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, 0, 0,
      convert_I420_AYUV},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_Y42B, FALSE, FALSE, 0, 0,
      convert_I420_Y42B},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_Y444, FALSE, FALSE, 0, 0,
      convert_I420_Y444},

  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_I420, TRUE, FALSE, 0, 0,
      convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_YV12, TRUE, FALSE, 0, 0,
      convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, 0, 0,
      convert_UYVY_YUY2},       /* alias */
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, 0, 0,
      convert_YUY2_AYUV},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, 0, 0,
      convert_YUY2_Y42B},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, 0, 0,
      convert_YUY2_Y444},

  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_I420, TRUE, FALSE, 0, 0,
      convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_YV12, TRUE, FALSE, 0, 0,
      convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, 0, 0,
      convert_UYVY_YUY2},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, 0, 0,
      convert_UYVY_AYUV},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, 0, 0,
      convert_UYVY_Y42B},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, 0, 0,
      convert_UYVY_Y444},

  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_I420, FALSE, FALSE, 1, 1,
      convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_YV12, FALSE, FALSE, 1, 1,
      convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, 1, 0,
      convert_AYUV_YUY2},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, 1, 0,
      convert_AYUV_UYVY},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, 1, 0,
      convert_AYUV_Y42B},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, 0, 0,
      convert_AYUV_Y444},

  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_I420, FALSE, FALSE, 0, 0,
      convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_YV12, FALSE, FALSE, 0, 0,
      convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, 0, 0,
      convert_Y42B_YUY2},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, 0, 0,
      convert_Y42B_UYVY},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, 1, 0,
      convert_Y42B_AYUV},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, 0, 0,
      convert_Y42B_Y444},

  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_I420, FALSE, FALSE, 1, 0,
      convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_YV12, FALSE, FALSE, 1, 0,
      convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, 1, 0,
      convert_Y444_YUY2},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, 1, 0,
      convert_Y444_UYVY},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, 0, 0,
      convert_Y444_AYUV},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, 1, 0,
      convert_Y444_Y42B},

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_ARGB, TRUE, TRUE, 0, 0,
      convert_AYUV_ARGB},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_BGRA, TRUE, TRUE, 0, 0,
      convert_AYUV_BGRA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_xRGB, TRUE, TRUE, 0, 0,
      convert_AYUV_ARGB},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_BGRx, TRUE, TRUE, 0, 0,
      convert_AYUV_BGRA},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_ABGR, TRUE, TRUE, 0, 0,
      convert_AYUV_ABGR},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGBA, TRUE, TRUE, 0, 0,
      convert_AYUV_RGBA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_xBGR, TRUE, TRUE, 0, 0,
      convert_AYUV_ABGR},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGBx, TRUE, TRUE, 0, 0,
      convert_AYUV_RGBA},       /* alias */

  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_BGRA, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_BGRx, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_BGRA, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_BGRx, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
#endif
};

static gboolean
video_converter_lookup_fastpath (GstVideoConverter * convert)
{
  int i;
  GstVideoFormat in_format, out_format;
  GstVideoTransferFunction in_transf, out_transf;
  gboolean interlaced, same_matrix, same_primaries;
  gint width, height;

  width = GST_VIDEO_INFO_WIDTH (&convert->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&convert->in_info);
  /* no scaling in fastpath */
  if (width != convert->out_width || height != convert->out_height)
    return FALSE;

  /* we don't do gamma conversion in fastpath */
  in_transf = convert->in_info.colorimetry.transfer;
  out_transf = convert->out_info.colorimetry.transfer;
  if (CHECK_GAMMA_REMAP (convert) && in_transf != out_transf)
    return FALSE;

  in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);

  if (CHECK_MATRIX_NO_YUV (convert)) {
    same_matrix = TRUE;
  } else {
    GstVideoColorMatrix in_matrix, out_matrix;

    in_matrix = convert->in_info.colorimetry.matrix;
    out_matrix = convert->out_info.colorimetry.matrix;
    same_matrix = in_matrix == out_matrix;
  }

  if (CHECK_PRIMARIES_NONE (convert)) {
    same_primaries = TRUE;
  } else {
    GstVideoColorPrimaries in_primaries, out_primaries;

    in_primaries = convert->in_info.colorimetry.primaries;
    out_primaries = convert->out_info.colorimetry.primaries;
    same_primaries = in_primaries == out_primaries;
  }

  interlaced = GST_VIDEO_INFO_IS_INTERLACED (&convert->in_info);
  interlaced |= GST_VIDEO_INFO_IS_INTERLACED (&convert->out_info);

  for (i = 0; i < sizeof (transforms) / sizeof (transforms[0]); i++) {
    if (transforms[i].in_format == in_format &&
        transforms[i].out_format == out_format &&
        (transforms[i].keeps_interlaced || !interlaced) &&
        (transforms[i].needs_color_matrix || (same_matrix && same_primaries)) &&
        (transforms[i].width_align & width) == 0 &&
        (transforms[i].height_align & height) == 0) {
      GST_DEBUG ("using fastpath");
      if (transforms[i].needs_color_matrix)
        video_converter_compute_matrix (convert);
      convert->convert = transforms[i].convert;
      alloc_tmplines (convert, 1, 0, GST_VIDEO_INFO_WIDTH (&convert->in_info));
      return TRUE;
    }
  }
  GST_DEBUG ("no fastpath found");
  return FALSE;
}
