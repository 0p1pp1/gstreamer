/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * Copyright (c) 2018, Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <va/va.h>
#include <va/va_drmcommon.h>
#include <unistd.h>
#include "gstmsdkallocator.h"
#include "gstmsdkallocator_libva.h"
#include "msdk_libva.h"

#include <gst/va/gstvaallocator.h>

#define GST_MSDK_FRAME_SURFACE gst_msdk_frame_surface_quark_get ()
static GQuark
gst_msdk_frame_surface_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstMsdkFrameSurface");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

mfxStatus
gst_msdk_frame_alloc (mfxHDL pthis, mfxFrameAllocRequest * req,
    mfxFrameAllocResponse * resp)
{
  VAStatus va_status;
  mfxStatus status = MFX_ERR_NONE;
  gint i;
  guint format;
  guint va_fourcc = 0;
  VASurfaceID *surfaces = NULL;
  VASurfaceAttrib attribs[2];
  guint num_attribs = 0;
  mfxMemId *mids = NULL;
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  GstMsdkMemoryID *msdk_mids = NULL;
  GstMsdkAllocResponse *msdk_resp = NULL;
  mfxU32 fourcc = req->Info.FourCC;
  mfxU16 surfaces_num = req->NumFrameSuggested;

  /* MFX_MAKEFOURCC('V','P','8','S') is used for MFX_FOURCC_VP9_SEGMAP surface
   * in MSDK and this surface is an internal surface. The external allocator
   * shouldn't be used for this surface allocation
   *
   * See https://github.com/Intel-Media-SDK/MediaSDK/issues/762
   */
  if (req->Type & MFX_MEMTYPE_INTERNAL_FRAME
      && fourcc == MFX_MAKEFOURCC ('V', 'P', '8', 'S'))
    return MFX_ERR_UNSUPPORTED;

  if (req->Type & MFX_MEMTYPE_EXTERNAL_FRAME) {
    GstMsdkAllocResponse *cached =
        gst_msdk_context_get_cached_alloc_responses_by_request (context, req);
    if (cached) {
      /* check if enough frames were allocated */
      if (req->NumFrameSuggested > cached->response.NumFrameActual)
        return MFX_ERR_MEMORY_ALLOC;

      *resp = cached->response;
      g_atomic_int_inc (&cached->refcount);
      return MFX_ERR_NONE;
    }
  }

  /* The VA API does not define any surface types and the application can use either
   * MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET or
   * MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET to indicate data in video memory.
   */
  if (!(req->Type & (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET |
              MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET)))
    return MFX_ERR_UNSUPPORTED;

  va_fourcc = gst_msdk_get_va_fourcc_from_mfx_fourcc (fourcc);

  msdk_mids =
      (GstMsdkMemoryID *) g_slice_alloc0 (surfaces_num *
      sizeof (GstMsdkMemoryID));
  mids = (mfxMemId *) g_slice_alloc0 (surfaces_num * sizeof (mfxMemId));
  surfaces =
      (VASurfaceID *) g_slice_alloc0 (surfaces_num * sizeof (VASurfaceID));
  msdk_resp =
      (GstMsdkAllocResponse *) g_slice_alloc0 (sizeof (GstMsdkAllocResponse));

  if (va_fourcc != VA_FOURCC_P208) {
    attribs[0].type = VASurfaceAttribPixelFormat;
    attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[0].value.type = VAGenericValueTypeInteger;
    attribs[0].value.value.i = va_fourcc;
    num_attribs = 1;

    /* set VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER flag for encoding */
#if (MFX_VERSION >= 1025)
    if ((req->Type & MFX_MEMTYPE_VIDEO_MEMORY_ENCODER_TARGET) &&
        (req->Type & MFX_MEMTYPE_FROM_ENCODE)) {
      attribs[1].type = VASurfaceAttribUsageHint;
      attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
      attribs[1].value.type = VAGenericValueTypeInteger;
      attribs[1].value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER;
      num_attribs = 2;
    }
#endif

    format =
        gst_msdk_get_va_rt_format_from_mfx_rt_format (req->Info.ChromaFormat);

    if (format == VA_RT_FORMAT_YUV420 && va_fourcc == VA_FOURCC_P010)
#if VA_CHECK_VERSION(1, 2, 0)
      format = VA_RT_FORMAT_YUV420_10;
#else
      format = VA_RT_FORMAT_YUV420_10BPP;
#endif

#if VA_CHECK_VERSION(1, 4, 1)
    if (format == VA_RT_FORMAT_YUV444 && va_fourcc == VA_FOURCC_A2R10G10B10)
      format = VA_RT_FORMAT_RGB32_10;
#endif

#if ((MFX_VERSION >= 1027) && VA_CHECK_VERSION(1, 2, 0))
    if (format == VA_RT_FORMAT_YUV422 && va_fourcc == VA_FOURCC_Y210)
      format = VA_RT_FORMAT_YUV422_10;
    else if (format == VA_RT_FORMAT_YUV444 && va_fourcc == VA_FOURCC_Y410)
      format = VA_RT_FORMAT_YUV444_10;
#endif

#if ((MFX_VERSION >= 1031) && VA_CHECK_VERSION(1, 2, 0))
    if (format == VA_RT_FORMAT_YUV420 && va_fourcc == VA_FOURCC_P016)
      format = VA_RT_FORMAT_YUV420_12;

    if (format == VA_RT_FORMAT_YUV422 && va_fourcc == VA_FOURCC_Y216)
      format = VA_RT_FORMAT_YUV422_12;

    if (format == VA_RT_FORMAT_YUV444 && va_fourcc == VA_FOURCC_Y416)
      format = VA_RT_FORMAT_YUV444_12;
#endif

#if (MFX_VERSION >= 2004)
    if (format == VA_RT_FORMAT_YUV444 && (va_fourcc == VA_FOURCC_RGBP
            || va_fourcc == VA_FOURCC_BGRP))
      format = VA_RT_FORMAT_RGBP;
#endif

    va_status = vaCreateSurfaces (gst_msdk_context_get_handle (context),
        format,
        req->Info.Width, req->Info.Height, surfaces, surfaces_num, attribs,
        num_attribs);

    status = gst_msdk_get_mfx_status_from_va_status (va_status);
    if (status != MFX_ERR_NONE) {
      GST_WARNING ("failed to create VA surface");
      return status;
    }

    for (i = 0; i < surfaces_num; i++) {
      /* Get dmabuf handle if MFX_MEMTYPE_EXPORT_FRAME */
      if (req->Type & MFX_MEMTYPE_EXPORT_FRAME) {
        VADRMPRIMESurfaceDescriptor va_desc = { 0 };
        uint32_t export_flags = VA_EXPORT_SURFACE_SEPARATE_LAYERS |
            VA_EXPORT_SURFACE_READ_WRITE;

        va_status =
            vaExportSurfaceHandle (gst_msdk_context_get_handle (context),
            surfaces[i], VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, export_flags,
            &va_desc);

        status = gst_msdk_get_mfx_status_from_va_status (va_status);

        if (MFX_ERR_NONE != status) {
          GST_ERROR ("Failed to export surface");
          return status;
        }

        g_assert (va_desc.num_objects);

        /* This plugin supports single object only */
        if (va_desc.num_objects > 1) {
          GST_ERROR ("Can not support multiple objects");
          return MFX_ERR_UNSUPPORTED;
        }

        msdk_mids[i].desc = va_desc;
      }

      /* Don't use image for DMABuf */
      msdk_mids[i].image.image_id = VA_INVALID_ID;
      msdk_mids[i].image.buf = VA_INVALID_ID;

      msdk_mids[i].surface = &surfaces[i];
      mids[i] = (mfxMemId *) & msdk_mids[i];
    }
  } else {
    /* This is requested from the driver when h265 encoding.
     * These buffers will be used inside the driver and released by
     * gst_msdk_frame_free functions. Application doesn't need to handle these buffers.
     *
     * See https://github.com/Intel-Media-SDK/samples/issues/13 for more details.
     */
    VAContextID context_id = req->AllocId;
    gint width32 = 32 * ((req->Info.Width + 31) >> 5);
    gint height32 = 32 * ((req->Info.Height + 31) >> 5);
    guint64 codedbuf_size = (width32 * height32) * 400LL / (16 * 16);

    for (i = 0; i < surfaces_num; i++) {
      VABufferID coded_buf;

      va_status = vaCreateBuffer (gst_msdk_context_get_handle (context),
          context_id, VAEncCodedBufferType, codedbuf_size, 1, NULL, &coded_buf);

      status = gst_msdk_get_mfx_status_from_va_status (va_status);
      if (status < MFX_ERR_NONE) {
        GST_ERROR ("failed to create buffer");
        return status;
      }

      surfaces[i] = coded_buf;
      msdk_mids[i].surface = &surfaces[i];
      msdk_mids[i].fourcc = fourcc;

      /* Don't use image for P208 */
      msdk_mids[i].image.image_id = VA_INVALID_ID;
      msdk_mids[i].image.buf = VA_INVALID_ID;

      mids[i] = (mfxMemId *) & msdk_mids[i];
    }
  }

  resp->mids = mids;
  resp->NumFrameActual = surfaces_num;

  msdk_resp->response = *resp;
  msdk_resp->request = *req;
  msdk_resp->refcount = 1;

  gst_msdk_context_add_alloc_response (context, msdk_resp);

  return status;
}

mfxStatus
gst_msdk_frame_free (mfxHDL pthis, mfxFrameAllocResponse * resp)
{
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  VAStatus va_status = VA_STATUS_SUCCESS;
  mfxStatus status;
  GstMsdkMemoryID *mem_id;
  VADisplay dpy;
  gint i;
  GstMsdkAllocResponse *cached = NULL;

  cached = gst_msdk_context_get_cached_alloc_responses (context, resp);

  if (cached) {
    if (!g_atomic_int_dec_and_test (&cached->refcount))
      return MFX_ERR_NONE;
  } else
    return MFX_ERR_NONE;

  if (!gst_msdk_context_remove_alloc_response (context, resp))
    return MFX_ERR_NONE;

  mem_id = resp->mids[0];
  dpy = gst_msdk_context_get_handle (context);

  if (mem_id->fourcc != MFX_FOURCC_P8) {
    /* Make sure that all the vaImages are destroyed */
    for (i = 0; i < resp->NumFrameActual; i++) {
      GstMsdkMemoryID *mem = resp->mids[i];

      /* Release prime fd if used */
      if (mem->desc.num_objects) {
        g_assert (mem->desc.num_objects == 1);
        close (mem->desc.objects[0].fd);
        mem->desc.num_objects = 0;
      }

      if (mem->image.image_id != VA_INVALID_ID &&
          vaDestroyImage (dpy, mem->image.image_id) == VA_STATUS_SUCCESS) {
        mem_id->image.image_id = VA_INVALID_ID;
        mem_id->image.buf = VA_INVALID_ID;
      }
    }

    va_status =
        vaDestroySurfaces (dpy, (VASurfaceID *) mem_id->surface,
        resp->NumFrameActual);
  } else {
    VASurfaceID *surfaces = mem_id->surface;

    for (i = 0; i < resp->NumFrameActual; i++) {
      va_status = vaDestroyBuffer (dpy, surfaces[i]);
    }
  }

  g_slice_free1 (resp->NumFrameActual * sizeof (VASurfaceID), mem_id->surface);
  g_slice_free1 (resp->NumFrameActual * sizeof (GstMsdkMemoryID), mem_id);
  g_slice_free1 (resp->NumFrameActual * sizeof (mfxMemId), resp->mids);

  status = gst_msdk_get_mfx_status_from_va_status (va_status);
  return status;
}

mfxStatus
gst_msdk_frame_lock (mfxHDL pthis, mfxMemId mid, mfxFrameData * data)
{
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  VAStatus va_status;
  mfxStatus status;
  mfxU8 *buf = NULL;
  VASurfaceID *va_surface;
  VADisplay dpy;
  GstMsdkMemoryID *mem_id;

  mem_id = (GstMsdkMemoryID *) mid;
  va_surface = mem_id->surface;
  dpy = gst_msdk_context_get_handle (context);

  if (mem_id->desc.num_objects) {
    GST_WARNING ("Couldn't map the buffer since dmabuf is already in use");
    return MFX_ERR_LOCK_MEMORY;
  }

  if (mem_id->fourcc != MFX_FOURCC_P8) {
    va_status = vaDeriveImage (dpy, *va_surface, &mem_id->image);
    status = gst_msdk_get_mfx_status_from_va_status (va_status);

    if (status != MFX_ERR_NONE) {
      GST_WARNING ("failed to derive image");
      return status;
    }

    va_status = vaMapBuffer (dpy, mem_id->image.buf, (void **) &buf);
    status = gst_msdk_get_mfx_status_from_va_status (va_status);

    if (status != MFX_ERR_NONE) {
      GST_WARNING ("failed to map");
      if (vaDestroyImage (dpy, mem_id->image.image_id) == VA_STATUS_SUCCESS) {
        mem_id->image.image_id = VA_INVALID_ID;
        mem_id->image.buf = VA_INVALID_ID;
      }
      return status;
    }

    switch (mem_id->image.format.fourcc) {
      case VA_FOURCC_NV12:
      case VA_FOURCC_P010:
      case VA_FOURCC_P016:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->UV = buf + mem_id->image.offsets[1];
        break;
      case VA_FOURCC_YV12:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = buf + mem_id->image.offsets[2];
        data->V = buf + mem_id->image.offsets[1];
        break;
      case VA_FOURCC_YUY2:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = data->Y + 1;
        data->V = data->Y + 3;
        break;
      case VA_FOURCC_UYVY:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = data->U + 1;
        data->V = data->U + 2;
        break;
      case VA_FOURCC_ARGB:
        data->Pitch = mem_id->image.pitches[0];
        data->B = buf + mem_id->image.offsets[0];
        data->G = data->B + 1;
        data->R = data->B + 2;
        data->A = data->B + 3;
        break;
#if (MFX_VERSION >= 1028)
      case VA_FOURCC_RGB565:
        data->Pitch = mem_id->image.pitches[0];
        data->R = buf + mem_id->image.offsets[0];
        data->G = data->R;
        data->B = data->R;
        break;
#endif
      case VA_FOURCC_AYUV:
        data->PitchHigh = (mfxU16) (mem_id->image.pitches[0] / (1 << 16));
        data->PitchLow = (mfxU16) (mem_id->image.pitches[0] % (1 << 16));
        data->V = buf + mem_id->image.offsets[0];
        data->U = data->V + 1;
        data->Y = data->V + 2;
        data->A = data->V + 3;
        break;
#if VA_CHECK_VERSION(1, 4, 1)
      case VA_FOURCC_A2R10G10B10:
        data->Pitch = mem_id->image.pitches[0];
        data->R = buf + mem_id->image.offsets[0];
        data->G = data->R;
        data->B = data->R;
        data->A = data->R;
        break;
#endif
#if VA_CHECK_VERSION(1, 2, 0)
      case VA_FOURCC_Y210:
      case VA_FOURCC_Y216:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = data->Y + 2;
        data->V = data->Y + 6;
        break;
      case VA_FOURCC_Y410:
        data->Pitch = mem_id->image.pitches[0];
        data->U = buf + mem_id->image.offsets[0];       /* data->Y410 */
        break;
      case VA_FOURCC_Y416:
        data->Pitch = mem_id->image.pitches[0];
        data->U = buf + mem_id->image.offsets[0];
        data->Y = data->U + 2;
        data->V = data->U + 4;
        data->A = data->U + 6;
        break;
#endif
      case VA_FOURCC_ABGR:
        data->Pitch = mem_id->image.pitches[0];
        data->R = buf + mem_id->image.offsets[0];
        data->G = data->R + 1;
        data->B = data->R + 2;
        data->A = data->R + 3;
        break;

#if (MFX_VERSION >= 2004)
      case VA_FOURCC_RGBP:
        data->Pitch = mem_id->image.pitches[0];
        data->R = buf + mem_id->image.offsets[0];
        data->G = buf + mem_id->image.offsets[1];
        data->B = buf + mem_id->image.offsets[2];
        break;
      case VA_FOURCC_BGRP:
        data->Pitch = mem_id->image.pitches[0];
        data->B = buf + mem_id->image.offsets[0];
        data->G = buf + mem_id->image.offsets[1];
        data->R = buf + mem_id->image.offsets[2];
        break;
#endif

      default:
        g_assert_not_reached ();
        break;
    }
  } else {
    VACodedBufferSegment *coded_buffer_segment;
    va_status =
        vaMapBuffer (dpy, *va_surface, (void **) (&coded_buffer_segment));
    status = gst_msdk_get_mfx_status_from_va_status (va_status);
    if (MFX_ERR_NONE == status)
      data->Y = (mfxU8 *) coded_buffer_segment->buf;
  }

  return status;
}

mfxStatus
gst_msdk_frame_unlock (mfxHDL pthis, mfxMemId mid, mfxFrameData * ptr)
{
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  VAStatus va_status;
  mfxStatus status;
  VADisplay dpy;
  GstMsdkMemoryID *mem_id;

  mem_id = (GstMsdkMemoryID *) mid;
  dpy = gst_msdk_context_get_handle (context);

  g_assert (mem_id->desc.num_objects == 0);

  if (mem_id->fourcc != MFX_FOURCC_P8) {
    vaUnmapBuffer (dpy, mem_id->image.buf);
    va_status = vaDestroyImage (dpy, mem_id->image.image_id);

    if (va_status == VA_STATUS_SUCCESS) {
      mem_id->image.image_id = VA_INVALID_ID;
      mem_id->image.buf = VA_INVALID_ID;
    }
  } else {
    va_status = vaUnmapBuffer (dpy, *(mem_id->surface));
  }

  status = gst_msdk_get_mfx_status_from_va_status (va_status);

  return status;
}

mfxStatus
gst_msdk_frame_get_hdl (mfxHDL pthis, mfxMemId mid, mfxHDL * hdl)
{
  GstMsdkMemoryID *mem_id;

  if (!hdl || !mid)
    return MFX_ERR_INVALID_HANDLE;

  mem_id = mid;
  *hdl = mem_id->surface;

  return MFX_ERR_NONE;
}

void
gst_msdk_set_frame_allocator (GstMsdkContext * context)
{
  mfxFrameAllocator gst_msdk_frame_allocator = {
    .pthis = context,
    .Alloc = gst_msdk_frame_alloc,
    .Lock = gst_msdk_frame_lock,
    .Unlock = gst_msdk_frame_unlock,
    .GetHDL = gst_msdk_frame_get_hdl,
    .Free = gst_msdk_frame_free,
  };

  gst_msdk_context_set_frame_allocator (context, &gst_msdk_frame_allocator);
}

gboolean
gst_msdk_get_dmabuf_info_from_surface (mfxFrameSurface1 * surface,
    gint * handle, gsize * size)
{
  GstMsdkMemoryID *mem_id;
  g_return_val_if_fail (surface, FALSE);

  mem_id = (GstMsdkMemoryID *) surface->Data.MemId;

  g_assert (mem_id->desc.num_objects == 1);

  if (handle)
    *handle = mem_id->desc.objects[0].fd;
  if (size)
    *size = mem_id->desc.objects[0].size;

  return TRUE;
}

gboolean
gst_msdk_export_dmabuf_to_vasurface (GstMsdkContext * context,
    GstVideoInfo * vinfo, gint fd, VASurfaceID * surface_id)
{
  GstVideoFormat format;
  guint width, height, size, i;
  unsigned long extbuf_handle;
  guint va_fourcc = 0, va_chroma = 0;
  VASurfaceAttrib attribs[2], *attrib;
  VASurfaceAttribExternalBuffers extbuf;
  VAStatus va_status;
  mfxStatus status = MFX_ERR_NONE;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (vinfo != NULL, FALSE);
  g_return_val_if_fail (fd >= 0, FALSE);

  extbuf_handle = (guintptr) (fd);

  format = GST_VIDEO_INFO_FORMAT (vinfo);
  width = GST_VIDEO_INFO_WIDTH (vinfo);
  height = GST_VIDEO_INFO_HEIGHT (vinfo);
  size = GST_VIDEO_INFO_SIZE (vinfo);

  /* Fixme: Move to common format handling util */
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      va_chroma = VA_RT_FORMAT_YUV420;
      va_fourcc = VA_FOURCC_NV12;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      va_chroma = VA_RT_FORMAT_YUV444;
      va_fourcc = VA_FOURCC_BGRA;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      va_chroma = VA_RT_FORMAT_YUV422;
      va_fourcc = VA_FOURCC_YUY2;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
#if VA_CHECK_VERSION(1, 2, 0)
      va_chroma = VA_RT_FORMAT_YUV420_10;
#else
      va_chroma = VA_RT_FORMAT_YUV420_10BPP;
#endif
      va_fourcc = VA_FOURCC_P010;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      va_chroma = VA_RT_FORMAT_YUV422;
      va_fourcc = VA_FOURCC_UYVY;
      break;
#if (MFX_VERSION >= 1028)
    case GST_VIDEO_FORMAT_RGB16:
      va_chroma = VA_RT_FORMAT_RGB16;
      va_fourcc = VA_FOURCC_RGB565;
      break;
#endif
    case GST_VIDEO_FORMAT_VUYA:
      va_chroma = VA_RT_FORMAT_YUV444;
      va_fourcc = VA_FOURCC_AYUV;
      break;
#if VA_CHECK_VERSION(1, 4, 1)
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      va_chroma = VA_RT_FORMAT_RGB32_10;
      va_fourcc = VA_FOURCC_A2R10G10B10;
      break;
#endif
#if VA_CHECK_VERSION(1, 2, 0)
    case GST_VIDEO_FORMAT_Y210:
      va_chroma = VA_RT_FORMAT_YUV422_10;
      va_fourcc = VA_FOURCC_Y210;
      break;
    case GST_VIDEO_FORMAT_Y410:
      va_chroma = VA_RT_FORMAT_YUV444_10;
      va_fourcc = VA_FOURCC_Y410;
      break;
    case GST_VIDEO_FORMAT_P012_LE:
      va_chroma = VA_RT_FORMAT_YUV420_12;
      va_fourcc = VA_FOURCC_P016;
      break;
    case GST_VIDEO_FORMAT_Y212_LE:
      va_chroma = VA_RT_FORMAT_YUV422_12;
      va_fourcc = VA_FOURCC_Y216;
      break;
    case GST_VIDEO_FORMAT_Y412_LE:
      va_chroma = VA_RT_FORMAT_YUV444_12;
      va_fourcc = VA_FOURCC_Y416;
      break;
#endif
#if (MFX_VERSION >= 2004)
    case GST_VIDEO_FORMAT_RGBP:
      va_chroma = VA_RT_FORMAT_RGBP;
      va_fourcc = VA_FOURCC_RGBP;
      break;
    case GST_VIDEO_FORMAT_BGRP:
      va_chroma = VA_RT_FORMAT_RGBP;
      va_fourcc = VA_FOURCC_BGRP;
      break;
#endif
    default:
      goto error_unsupported_format;
  }

  /* Fill the VASurfaceAttribExternalBuffers */
  extbuf.pixel_format = va_fourcc;
  extbuf.width = width;
  extbuf.height = height;
  extbuf.data_size = size;
  extbuf.num_planes = GST_VIDEO_INFO_N_PLANES (vinfo);
  for (i = 0; i < extbuf.num_planes; i++) {
    extbuf.pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i);
    extbuf.offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET (vinfo, i);
  }
  extbuf.buffers = (uintptr_t *) & extbuf_handle;
  extbuf.num_buffers = 1;
  extbuf.flags = 0;
  extbuf.private_data = NULL;

  /* Fill the Surface Attributes */
  attrib = attribs;
  attrib->type = VASurfaceAttribMemoryType;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->value.type = VAGenericValueTypeInteger;
  attrib->value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
  attrib++;
  attrib->type = VASurfaceAttribExternalBufferDescriptor;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->value.type = VAGenericValueTypePointer;
  attrib->value.value.p = &extbuf;
  attrib++;

  va_status = vaCreateSurfaces (gst_msdk_context_get_handle (context),
      va_chroma, width, height, surface_id, 1, attribs, attrib - attribs);
  status = gst_msdk_get_mfx_status_from_va_status (va_status);
  if (status != MFX_ERR_NONE)
    goto error_create_surface;

  return TRUE;

error_unsupported_format:
  {
    GST_ERROR ("Unsupported Video format %s, Can't export dmabuf to vaSurface",
        gst_video_format_to_string (format));
    return FALSE;
  }
error_create_surface:
  {
    GST_ERROR ("Failed to create the VASurface from DRM_PRIME FD");
    return FALSE;
  }
}

static VASurfaceID
_get_va_surface (GstBuffer * buf, GstVideoInfo * info,
    GstMsdkContext * msdk_context)
{
  VASurfaceID va_surface = VA_INVALID_ID;

  if (!info) {
    va_surface = gst_va_buffer_get_surface (buf);
  } else {
    /* Update offset/stride/size if there is VideoMeta attached to
     * the dma buffer, which is then used to get vasurface */
    GstMemory *mem;
    gint i, fd;
    GstVideoMeta *vmeta;

    vmeta = gst_buffer_get_video_meta (buf);
    if (vmeta) {
      if (GST_VIDEO_INFO_FORMAT (info) != vmeta->format ||
          GST_VIDEO_INFO_WIDTH (info) != vmeta->width ||
          GST_VIDEO_INFO_HEIGHT (info) != vmeta->height ||
          GST_VIDEO_INFO_N_PLANES (info) != vmeta->n_planes) {
        GST_ERROR ("VideoMeta attached to buffer is not matching"
            "the negotiated width/height/format");
        return va_surface;
      }
      for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); ++i) {
        GST_VIDEO_INFO_PLANE_OFFSET (info, i) = vmeta->offset[i];
        GST_VIDEO_INFO_PLANE_STRIDE (info, i) = vmeta->stride[i];
      }
      GST_VIDEO_INFO_SIZE (info) = gst_buffer_get_size (buf);
    }

    mem = gst_buffer_peek_memory (buf, 0);
    fd = gst_dmabuf_memory_get_fd (mem);
    if (fd < 0)
      return va_surface;
    /* export dmabuf to vasurface */
    if (!gst_msdk_export_dmabuf_to_vasurface (msdk_context, info, fd,
            &va_surface))
      return VA_INVALID_ID;
  }

  return va_surface;
}

/* Currently parameter map_flag is not useful on Linux */
GstMsdkSurface *
gst_msdk_import_to_msdk_surface (GstBuffer * buf, GstMsdkContext * msdk_context,
    GstVideoInfo * vinfo, guint map_flag)
{
  VASurfaceID va_surface = VA_INVALID_ID;
  GstMemory *mem = NULL;
  mfxFrameInfo frame_info = { 0, };
  GstMsdkSurface *msdk_surface = NULL;
  mfxFrameSurface1 *mfx_surface = NULL;
  GstMsdkMemoryID *msdk_mid = NULL;

  mem = gst_buffer_peek_memory (buf, 0);
  msdk_surface = g_slice_new0 (GstMsdkSurface);

  /* If buffer has qdata pointing to mfxFrameSurface1, directly extract it */
  if ((mfx_surface = gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (mem),
              GST_MSDK_FRAME_SURFACE))) {
    msdk_surface->surface = mfx_surface;
    msdk_surface->from_qdata = TRUE;
    return msdk_surface;
  }

  if (gst_msdk_is_va_mem (mem)) {
    va_surface = _get_va_surface (buf, NULL, NULL);
  } else if (gst_is_dmabuf_memory (mem)) {
    /* For dma memory, videoinfo is used with dma fd to create va surface. */
    GstVideoInfo info = *vinfo;
    va_surface = _get_va_surface (buf, &info, msdk_context);
  }

  if (va_surface == VA_INVALID_ID) {
    g_slice_free (GstMsdkSurface, msdk_surface);
    return NULL;
  }

  mfx_surface = g_slice_new0 (mfxFrameSurface1);
  msdk_mid = g_slice_new0 (GstMsdkMemoryID);

  msdk_mid->surface = g_slice_new0 (VASurfaceID);
  *msdk_mid->surface = va_surface;

  mfx_surface->Data.MemId = (mfxMemId) msdk_mid;

  gst_msdk_set_mfx_frame_info_from_video_info (&frame_info, vinfo);
  mfx_surface->Info = frame_info;

  /* Set mfxFrameSurface1 as qdata in buffer */
  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
      GST_MSDK_FRAME_SURFACE, mfx_surface, NULL);

  msdk_surface->surface = mfx_surface;

  return msdk_surface;
}

/**
 * gst_msdk_replace_mfx_memid:
 * This method replace the internal VA Suface in mfxSurface with a new one
 *
 * Caution: Not a thread-safe routine, this method is here to work around
 * the dmabuf-import use case with dynamic memID replacement where msdk
 * originally Inited with fake memIDs.
 *
 * Don't use anywhere else unless you really know what you are doing!
 */
gboolean
gst_msdk_replace_mfx_memid (GstMsdkContext * context,
    mfxFrameSurface1 * mfx_surface, VASurfaceID surface_id)
{
  GstMsdkMemoryID *msdk_mid = NULL;
  VADisplay dpy;
  VASurfaceID *old_surface_id;
  VAStatus va_status;
  mfxStatus status = MFX_ERR_NONE;

  g_return_val_if_fail (mfx_surface != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  msdk_mid = (GstMsdkMemoryID *) mfx_surface->Data.MemId;
  dpy = gst_msdk_context_get_handle (context);

  /* Destroy the underlined VAImage if already mapped */
  if (msdk_mid->image.image_id != VA_INVALID_ID
      && msdk_mid->image.buf != VA_INVALID_ID) {
    status =
        gst_msdk_frame_unlock ((mfxHDL) context, (mfxMemId) msdk_mid, NULL);
    if (status != MFX_ERR_NONE)
      goto error_destroy_va_image;
  }

  /* Destroy the associated VASurface */
  old_surface_id = msdk_mid->surface;
  if (*old_surface_id != VA_INVALID_ID) {
    va_status = vaDestroySurfaces (dpy, old_surface_id, 1);
    status = gst_msdk_get_mfx_status_from_va_status (va_status);
    if (status != MFX_ERR_NONE)
      goto error_destroy_va_surface;
  }

  *msdk_mid->surface = surface_id;

  return TRUE;

error_destroy_va_image:
  {
    GST_ERROR ("Failed to Destroy the VAImage");
    return FALSE;
  }
error_destroy_va_surface:
  {
    GST_ERROR ("Failed to Destroy the VASurfaceID %x", *old_surface_id);
    return FALSE;
  }
}
