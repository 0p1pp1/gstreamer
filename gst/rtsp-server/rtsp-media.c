/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "rtsp-media.h"

#define DEFAULT_SHARED         FALSE

enum
{
  PROP_0,
  PROP_SHARED,
  PROP_LAST
};

static void gst_rtsp_media_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_finalize (GObject * obj);

static gpointer do_loop (GstRTSPMediaClass *klass);
static gboolean default_handle_message (GstRTSPMedia *media, GstMessage *message);

G_DEFINE_TYPE (GstRTSPMedia, gst_rtsp_media, G_TYPE_OBJECT);

static void
gst_rtsp_media_class_init (GstRTSPMediaClass * klass)
{
  GObjectClass *gobject_class;
  GError *error = NULL;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_media_get_property;
  gobject_class->set_property = gst_rtsp_media_set_property;
  gobject_class->finalize = gst_rtsp_media_finalize;

  g_object_class_install_property (gobject_class, PROP_SHARED,
      g_param_spec_boolean ("shared", "Shared", "If this media pipeline can be shared",
          DEFAULT_SHARED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->context = g_main_context_new ();
  klass->loop = g_main_loop_new (klass->context, TRUE);

  klass->thread = g_thread_create ((GThreadFunc) do_loop, klass, TRUE, &error);
  if (error != NULL) {
    g_critical ("could not start bus thread: %s", error->message);
  }
  klass->handle_message = default_handle_message;
}

static void
gst_rtsp_media_init (GstRTSPMedia * media)
{
  media->streams = g_array_new (FALSE, TRUE, sizeof (GstRTSPMediaStream *));
  media->complete = FALSE;
  media->is_live = FALSE;
  media->buffering = FALSE;
}

static void
gst_rtsp_media_stream_free (GstRTSPMediaStream *stream)
{
  if (stream->session)
    g_object_unref (stream->session);

  if (stream->caps)
    gst_caps_unref (stream->caps);

  g_free (stream);
}

static void
gst_rtsp_media_finalize (GObject * obj)
{
  GstRTSPMedia *media;
  guint i;

  media = GST_RTSP_MEDIA (obj);

  g_message ("finalize media %p", media);

  if (media->pipeline) {
    gst_element_set_state (media->pipeline, GST_STATE_NULL);
    gst_object_unref (media->pipeline);
  }

  for (i = 0; i < media->streams->len; i++) {
    GstRTSPMediaStream *stream;

    stream = g_array_index (media->streams, GstRTSPMediaStream *, i);

    gst_rtsp_media_stream_free (stream);
  }
  g_array_free (media->streams, TRUE);

  if (media->source) {
    g_source_destroy (media->source);
    g_source_unref (media->source);
  }

  G_OBJECT_CLASS (gst_rtsp_media_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec)
{
  GstRTSPMedia *media = GST_RTSP_MEDIA (object);

  switch (propid) {
    case PROP_SHARED:
      g_value_set_boolean (value, gst_rtsp_media_is_shared (media));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_media_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec)
{
  GstRTSPMedia *media = GST_RTSP_MEDIA (object);

  switch (propid) {
    case PROP_SHARED:
      gst_rtsp_media_set_shared (media, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static gpointer
do_loop (GstRTSPMediaClass *klass)
{
  g_message ("enter mainloop");
  g_main_loop_run (klass->loop);
  g_message ("exit mainloop");

  return NULL;
}

/**
 * gst_rtsp_media_new:
 *
 * Create a new #GstRTSPMedia instance. The #GstRTSPMedia object contains the
 * element to produde RTP data for one or more related (audio/video/..) 
 * streams.
 *
 * Returns: a new #GstRTSPMedia object.
 */
GstRTSPMedia *
gst_rtsp_media_new (void)
{
  GstRTSPMedia *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA, NULL);

  return result;
}

/**
 * gst_rtsp_media_set_shared:
 * @media: a #GstRTSPMedia
 * @shared: the new value
 *
 * Set or unset if the pipeline for @media can be shared will multiple clients.
 * When @shared is %TRUE, client requests for this media will share the media
 * pipeline.
 */
void
gst_rtsp_media_set_shared (GstRTSPMedia *media, gboolean shared)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  media->shared = shared;
}

/**
 * gst_rtsp_media_is_shared:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media can be shared between multiple clients.
 *
 * Returns: %TRUE if the media can be shared between clients.
 */
gboolean
gst_rtsp_media_is_shared (GstRTSPMedia *media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  return media->shared;
}

/**
 * gst_rtsp_media_n_streams:
 * @media: a #GstRTSPMedia
 *
 * Get the number of streams in this media.
 *
 * Returns: The number of streams.
 */
guint
gst_rtsp_media_n_streams (GstRTSPMedia *media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), 0);

  return media->streams->len;
}

/**
 * gst_rtsp_media_get_stream:
 * @media: a #GstRTSPMedia
 * @idx: the stream index
 *
 * Retrieve the stream with index @idx from @media.
 *
 * Returns: the #GstRTSPMediaStream at index @idx or %NULL when a stream with
 * that index did not exist.
 */
GstRTSPMediaStream *
gst_rtsp_media_get_stream (GstRTSPMedia *media, guint idx)
{
  GstRTSPMediaStream *res;
  
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  if (idx < media->streams->len)
    res = g_array_index (media->streams, GstRTSPMediaStream *, idx);
  else
    res = NULL;

  return res;
}

/* Allocate the udp ports and sockets */
static gboolean
alloc_udp_ports (GstRTSPMediaStream * stream)
{
  GstStateChangeReturn ret;
  GstElement *udpsrc0, *udpsrc1;
  GstElement *udpsink0, *udpsink1;
  gint tmp_rtp, tmp_rtcp;
  guint count;
  gint rtpport, rtcpport, sockfd;

  udpsrc0 = NULL;
  udpsrc1 = NULL;
  udpsink0 = NULL;
  udpsink1 = NULL;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  udpsrc0 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
  if (udpsrc0 == NULL)
    goto no_udp_protocol;
  g_object_set (G_OBJECT (udpsrc0), "port", tmp_rtp, NULL);

  ret = gst_element_set_state (udpsrc0, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    if (tmp_rtp != 0) {
      tmp_rtp += 2;
      if (++count > 20)
        goto no_ports;

      gst_element_set_state (udpsrc0, GST_STATE_NULL);
      gst_object_unref (udpsrc0);

      goto again;
    }
    goto no_udp_protocol;
  }

  g_object_get (G_OBJECT (udpsrc0), "port", &tmp_rtp, NULL);

  /* check if port is even */
  if ((tmp_rtp & 1) != 0) {
    /* port not even, close and allocate another */
    if (++count > 20)
      goto no_ports;

    gst_element_set_state (udpsrc0, GST_STATE_NULL);
    gst_object_unref (udpsrc0);

    tmp_rtp++;
    goto again;
  }

  /* allocate port+1 for RTCP now */
  udpsrc1 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
  if (udpsrc1 == NULL)
    goto no_udp_rtcp_protocol;

  /* set port */
  tmp_rtcp = tmp_rtp + 1;
  g_object_set (G_OBJECT (udpsrc1), "port", tmp_rtcp, NULL);

  ret = gst_element_set_state (udpsrc1, GST_STATE_PAUSED);
  /* tmp_rtcp port is busy already : retry to make rtp/rtcp pair */
  if (ret == GST_STATE_CHANGE_FAILURE) {

    if (++count > 20)
      goto no_ports;

    gst_element_set_state (udpsrc0, GST_STATE_NULL);
    gst_object_unref (udpsrc0);

    gst_element_set_state (udpsrc1, GST_STATE_NULL);
    gst_object_unref (udpsrc1);

    tmp_rtp += 2;
    goto again;
  }

  /* all fine, do port check */
  g_object_get (G_OBJECT (udpsrc0), "port", &rtpport, NULL);
  g_object_get (G_OBJECT (udpsrc1), "port", &rtcpport, NULL);

  /* this should not happen... */
  if (rtpport != tmp_rtp || rtcpport != tmp_rtcp)
    goto port_error;

  udpsink0 = gst_element_factory_make ("multiudpsink", NULL);
  if (!udpsink0)
    goto no_udp_protocol;

  g_object_get (G_OBJECT (udpsrc0), "sock", &sockfd, NULL);
  g_object_set (G_OBJECT (udpsink0), "sockfd", sockfd, NULL);
  g_object_set (G_OBJECT (udpsink0), "closefd", FALSE, NULL);

  udpsink1 = gst_element_factory_make ("multiudpsink", NULL);
  if (!udpsink1)
    goto no_udp_protocol;

  g_object_get (G_OBJECT (udpsrc1), "sock", &sockfd, NULL);
  g_object_set (G_OBJECT (udpsink1), "sockfd", sockfd, NULL);
  g_object_set (G_OBJECT (udpsink1), "closefd", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (udpsink1), "async", FALSE, NULL);

  /* we keep these elements, we configure all in configure_transport when the
   * server told us to really use the UDP ports. */
  stream->udpsrc[0] = udpsrc0;
  stream->udpsrc[1] = udpsrc1;
  stream->udpsink[0] = udpsink0;
  stream->udpsink[1] = udpsink1;
  stream->server_port.min = rtpport;
  stream->server_port.max = rtcpport;

  return TRUE;

  /* ERRORS */
no_udp_protocol:
  {
    goto cleanup;
  }
no_ports:
  {
    goto cleanup;
  }
no_udp_rtcp_protocol:
  {
    goto cleanup;
  }
port_error:
  {
    goto cleanup;
  }
cleanup:
  {
    if (udpsrc0) {
      gst_element_set_state (udpsrc0, GST_STATE_NULL);
      gst_object_unref (udpsrc0);
    }
    if (udpsrc1) {
      gst_element_set_state (udpsrc1, GST_STATE_NULL);
      gst_object_unref (udpsrc1);
    }
    if (udpsink0) {
      gst_element_set_state (udpsink0, GST_STATE_NULL);
      gst_object_unref (udpsink0);
    }
    if (udpsink1) {
      gst_element_set_state (udpsink1, GST_STATE_NULL);
      gst_object_unref (udpsink1);
    }
    return FALSE;
  }
}

static void
caps_notify (GstPad * pad, GParamSpec * unused, GstRTSPMediaStream * stream)
{
  gchar *capsstr;

  if (stream->caps)
    gst_caps_unref (stream->caps);
  if ((stream->caps = GST_PAD_CAPS (pad)))
    gst_caps_ref (stream->caps);

  capsstr = gst_caps_to_string (stream->caps);
  g_message ("stream %p received caps %s", stream, capsstr);
  g_free (capsstr);
}

static void
on_new_ssrc (GObject *session, GObject *source, GstRTSPMedia *media)
{
  g_message ("%p: new source %p", media, source);
}

static void
on_ssrc_active (GObject *session, GObject *source, GstRTSPMedia *media)
{
  g_message ("%p: source %p is active", media, source);
}

static void
on_bye_ssrc (GObject *session, GObject *source, GstRTSPMedia *media)
{
  g_message ("%p: source %p bye", media, source);
}

static void
on_bye_timeout (GObject *session, GObject *source, GstRTSPMedia *media)
{
  g_message ("%p: source %p bye timeout", media, source);
}

static void
on_timeout (GObject *session, GObject *source, GstRTSPMedia *media)
{
  g_message ("%p: source %p timeout", media, source);
}

/* prepare the pipeline objects to handle @stream in @media */
static gboolean
setup_stream (GstRTSPMediaStream *stream, guint idx, GstRTSPMedia *media)
{
  gchar *name;
  GstPad *pad;

  alloc_udp_ports (stream);

  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->udpsink[0]);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->udpsink[1]);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->udpsrc[0]);
  gst_bin_add (GST_BIN_CAST (media->pipeline), stream->udpsrc[1]);

  /* hook up the stream to the RTP session elements. */
  name = g_strdup_printf ("send_rtp_sink_%d", idx);
  stream->send_rtp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtp_src_%d", idx);
  stream->send_rtp_src = gst_element_get_static_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("send_rtcp_src_%d", idx);
  stream->send_rtcp_src = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);
  name = g_strdup_printf ("recv_rtcp_sink_%d", idx);
  stream->recv_rtcp_sink = gst_element_get_request_pad (media->rtpbin, name);
  g_free (name);

  /* get the session */
  g_signal_emit_by_name (media->rtpbin, "get-internal-session", idx,
		  &stream->session);

  g_signal_connect (stream->session, "on-new-ssrc", (GCallback) on_new_ssrc,
      media);
  g_signal_connect (stream->session, "on-ssrc-active", (GCallback) on_ssrc_active,
      media);
  g_signal_connect (stream->session, "on-bye-ssrc", (GCallback) on_bye_ssrc,
      media);
  g_signal_connect (stream->session, "on-bye-timeout", (GCallback) on_bye_timeout,
      media);
  g_signal_connect (stream->session, "on-timeout", (GCallback) on_timeout,
      media);

  /* link the RTP pad to the session manager */
  gst_pad_link (stream->srcpad, stream->send_rtp_sink);

  /* link udp elements */
  pad = gst_element_get_static_pad (stream->udpsink[0], "sink");
  gst_pad_link (stream->send_rtp_src, pad);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (stream->udpsink[1], "sink");
  gst_pad_link (stream->send_rtcp_src, pad);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (stream->udpsrc[1], "src");
  gst_pad_link (pad, stream->recv_rtcp_sink);
  gst_object_unref (pad);

  /* we set and keep these to playing so that they don't cause NO_PREROLL return
   * values */
  gst_element_set_state (stream->udpsrc[0], GST_STATE_PLAYING);
  gst_element_set_state (stream->udpsrc[1], GST_STATE_PLAYING);
  gst_element_set_locked_state (stream->udpsrc[0], TRUE);
  gst_element_set_locked_state (stream->udpsrc[1], TRUE);
 
  /* be notified of caps changes */
  stream->caps_sig = g_signal_connect (stream->send_rtp_sink, "notify::caps",
                  (GCallback) caps_notify, stream);

  stream->prepared = TRUE;

  return TRUE;
}

static void
unlock_streams (GstRTSPMedia *media)
{
  guint i, n_streams;

  /* unlock the udp src elements */
  n_streams = gst_rtsp_media_n_streams (media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;

    stream = gst_rtsp_media_get_stream (media, i);

    gst_element_set_locked_state (stream->udpsrc[0], FALSE);
    gst_element_set_locked_state (stream->udpsrc[1], FALSE);
  }
}

static void
collect_media_stats (GstRTSPMedia *media)
{
  GstFormat format;
  gint64 duration;

  media->range.unit = GST_RTSP_RANGE_NPT;
  media->range.min.type = GST_RTSP_TIME_SECONDS;
  media->range.min.seconds = 0.0;

  /* get the duration */
  format = GST_FORMAT_TIME;
  if (!gst_element_query_duration (media->pipeline, &format, &duration)) 
    duration = -1;

  if (duration == -1) {
    media->range.max.type = GST_RTSP_TIME_END;
    media->range.max.seconds = -1;
  }
  else {
    media->range.max.type = GST_RTSP_TIME_SECONDS;
    media->range.max.seconds = ((gdouble)duration) / GST_SECOND;
  }
}

static gboolean
default_handle_message (GstRTSPMedia *media, GstMessage *message)
{
  GstMessageType type;

  type = GST_MESSAGE_TYPE (message);

  switch (type) {
    case GST_MESSAGE_STATE_CHANGED:
      break;
    case GST_MESSAGE_BUFFERING:
    {
      gint percent;

      gst_message_parse_buffering (message, &percent);

      /* no state management needed for live pipelines */
      if (media->is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        media->buffering = FALSE;
        /* if the desired state is playing, go back */
        if (media->target_state == GST_STATE_PLAYING) {
          g_message ("Buffering done, setting pipeline to PLAYING");
          gst_element_set_state (media->pipeline, GST_STATE_PLAYING);
        }
	else {
          g_message ("Buffering done");
	}
      } else {
        /* buffering busy */
        if (media->buffering == FALSE) {
	  if (media->target_state == GST_STATE_PLAYING) {
            /* we were not buffering but PLAYING, PAUSE  the pipeline. */
            g_message ("Buffering, setting pipeline to PAUSED ...");
            gst_element_set_state (media->pipeline, GST_STATE_PAUSED);
	  }
	  else {
            g_message ("Buffering ...");
	  }
        }
        media->buffering = TRUE;
      }
      break;
    }
    case GST_MESSAGE_LATENCY:
    {
      gst_bin_recalculate_latency (GST_BIN_CAST (media->pipeline));
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      g_warning ("%p: got error %s (%s)", media, gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);
      break;
    }
    default:
      g_message ("%p: got message type %s", media, gst_message_type_get_name (type));
      break;
  }
  return TRUE;
}

static gboolean
bus_message (GstBus *bus, GstMessage *message, GstRTSPMedia *media)
{
  GstRTSPMediaClass *klass;
  gboolean ret;
  
  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  if (klass->handle_message)
    ret = klass->handle_message (media, message);
  else
    ret = FALSE;

  return ret;
}

/**
 * gst_rtsp_media_prepare:
 * @obj: a #GstRTSPMedia
 *
 * Prepare @media for streaming. This function will create the pipeline and
 * other objects to manage the streaming.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_prepare (GstRTSPMedia *media)
{
  GstStateChangeReturn ret;
  guint i, n_streams;
  GstRTSPMediaClass *klass;
  GstBus *bus;

  if (media->prepared)
    goto was_prepared;

  g_message ("preparing media %p", media);

  media->pipeline = gst_pipeline_new ("media-pipeline");
  bus = gst_pipeline_get_bus (GST_PIPELINE_CAST (media->pipeline));

  /* add the pipeline bus to our custom mainloop */
  media->source = gst_bus_create_watch (bus);
  gst_object_unref (bus);

  g_source_set_callback (media->source, (GSourceFunc) bus_message, media, NULL);

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  media->id = g_source_attach (media->source, klass->context);

  gst_bin_add (GST_BIN_CAST (media->pipeline), media->element);

  media->rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

  /* add stuf to the bin */
  gst_bin_add (GST_BIN (media->pipeline), media->rtpbin);

  /* link streams we already have */
  n_streams = gst_rtsp_media_n_streams (media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;

    stream = gst_rtsp_media_get_stream (media, i);

    setup_stream (stream, i, media);
  }

  /* first go to PAUSED */
  ret = gst_element_set_state (media->pipeline, GST_STATE_PAUSED);
  media->target_state = GST_STATE_PAUSED;

  switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
      break;
    case GST_STATE_CHANGE_ASYNC:
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      /* we need to go to PLAYING */
      g_message ("live media %p", media);
      media->is_live = TRUE;
      ret = gst_element_set_state (media->pipeline, GST_STATE_PLAYING);
      break;
    case GST_STATE_CHANGE_FAILURE:
    {
      unlock_streams (media);
      goto state_failed;
    }
  }

  /* now wait for all pads to be prerolled */
  ret = gst_element_get_state (media->pipeline, NULL, NULL, -1);

  /* and back to PAUSED for live pipelines */
  ret = gst_element_set_state (media->pipeline, GST_STATE_PAUSED);

  /* collect stats about the media */
  collect_media_stats (media);

  /* unlock the streams so that they follow the state changes from now on */
  unlock_streams (media);

  g_message ("object %p is prerolled", media);

  media->prepared = TRUE;

  return TRUE;

  /* OK */
was_prepared:
  {
    return TRUE;
  }
  /* ERRORS */
state_failed:
  {
    gst_element_set_state (media->pipeline, GST_STATE_NULL);
    return FALSE;
  }
}

/**
 * gst_rtsp_media_play:
 * @media: a #GstRTSPMedia
 * @transports: a GArray of #GstRTSPMediaTrans pointers
 *
 * Start playing @media for to the transports in @transports.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_play (GstRTSPMedia *media, GArray *transports)
{
  gint i;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (transports != NULL, FALSE);
  g_return_val_if_fail (media->prepared, FALSE);

  if (media->target_state == GST_STATE_PLAYING)
    return TRUE;

  for (i = 0; i < transports->len; i++) {
    GstRTSPMediaTrans *tr;
    GstRTSPMediaStream *stream;
    GstRTSPTransport *trans;

    /* we need a non-NULL entry in the array */
    tr = g_array_index (transports, GstRTSPMediaTrans *, i);
    if (tr == NULL)
      continue;

    /* we need a transport */
    if (!(trans = tr->transport))
      continue;

    /* get the stream and add the destinations */
    stream = gst_rtsp_media_get_stream (media, tr->idx);

    g_message ("adding %s:%d-%d", trans->destination, trans->client_port.min, trans->client_port.max);

    g_signal_emit_by_name (stream->udpsink[0], "add", trans->destination, trans->client_port.min, NULL);
    g_signal_emit_by_name (stream->udpsink[1], "add", trans->destination, trans->client_port.max, NULL);
  }

  g_message ("playing media %p", media);
  media->target_state = GST_STATE_PLAYING;
  ret = gst_element_set_state (media->pipeline, GST_STATE_PLAYING);

  return TRUE;
}

/**
 * gst_rtsp_media_pause:
 * @media: a #GstRTSPMedia
 * @transports: a array of #GstRTSPTransport pointers
 *
 * Pause playing @media for to the transports in @transports.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_pause (GstRTSPMedia *media, GArray *transports)
{
  gint i;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (transports != NULL, FALSE);
  g_return_val_if_fail (media->prepared, FALSE);

  if (media->target_state == GST_STATE_PAUSED)
    return TRUE;

  for (i = 0; i < transports->len; i++) {
    GstRTSPMediaTrans *tr;
    GstRTSPMediaStream *stream;
    GstRTSPTransport *trans;

    /* we need a non-NULL entry in the array */
    tr = g_array_index (transports, GstRTSPMediaTrans *, i);
    if (tr == NULL)
      continue;

    /* we need a transport */
    if (!(trans = tr->transport))
      continue;

    /* get the stream and add the destinations */
    stream = gst_rtsp_media_get_stream (media, tr->idx);

    g_message ("removing %s:%d-%d", trans->destination, trans->client_port.min, trans->client_port.max);

    g_signal_emit_by_name (stream->udpsink[0], "remove", trans->destination, trans->client_port.min, NULL);
    g_signal_emit_by_name (stream->udpsink[1], "remove", trans->destination, trans->client_port.max, NULL);
  }

  g_message ("pause media %p", media);
  media->target_state = GST_STATE_PAUSED;
  ret = gst_element_set_state (media->pipeline, GST_STATE_PAUSED);

  return TRUE;
}

/**
 * gst_rtsp_media_stream_stop:
 * @media: a #GstRTSPMedia
 * @transports: a GArray of #GstRTSPMediaTrans pointers
 *
 * Stop playing @media for to the transports in @transports.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_stop (GstRTSPMedia *media, GArray *transports)
{
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (transports != NULL, FALSE);
  g_return_val_if_fail (media->prepared, FALSE);

  if (media->target_state == GST_STATE_NULL)
    return TRUE;

  gst_rtsp_media_pause (media, transports);

  g_message ("stop media %p", media);
  media->target_state = GST_STATE_NULL;
  ret = gst_element_set_state (media->pipeline, GST_STATE_NULL);

  return TRUE;
}

