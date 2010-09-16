/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <ges/ges.h>
#include <gst/profile/gstprofile.h>
#include <regex.h>

/* GLOBAL VARIABLE */
static guint repeat = 0;
GESTimelinePipeline *pipeline = NULL;

gboolean pattern_source_fill_func (GESTimelineObject * object, GESTrackObject
    * trobject, GstElement * gnlobj, gpointer user_data);

void print_transition_list (void);

gboolean check_path (char *path);

GESTimelineObject *pattern_source_new (guint pattern);

gboolean check_time (char *time);

guint64 str_to_time (char *time);

void print_pattern_list (void);

gboolean thumbnail_cb (gpointer pipeline);

gboolean
thumbnail_cb (gpointer pipeline)
{
  static int i = 0;
  GESTimelinePipeline *p = (GESTimelinePipeline *) pipeline;
  gchar *filename;
  gboolean res;

  filename = g_strdup_printf ("thumbnail%d.jpg", i++);

  res = ges_timeline_pipeline_save_thumbnail (p, -1, -1,
      (gchar *) "image/jpeg", filename);

  g_free (filename);

  return res;
}

gboolean
pattern_source_fill_func (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj, gpointer user_data)
{
  guint pattern = GPOINTER_TO_UINT (user_data);
  GESTrack *track = trobject->track;
  GstElement *testsrc;

  g_assert (track);

  if ((track->type) == GES_TRACK_TYPE_VIDEO) {
    testsrc = gst_element_factory_make ("videotestsrc", NULL);
    g_object_set (testsrc, "pattern", pattern, NULL);
  } else if ((track->type) == GES_TRACK_TYPE_AUDIO) {
    testsrc = gst_element_factory_make ("audiotestsrc", NULL);
    g_object_set (testsrc, "volume", (gdouble) 0, NULL);
  } else
    return FALSE;

  return gst_bin_add (GST_BIN (gnlobj), testsrc);
}

GESTimelineObject *
pattern_source_new (guint pattern)
{
  GESTimelineObject *ret;
  ret = GES_TIMELINE_OBJECT (ges_timeline_test_source_new ());
  g_object_set (ret, "vpattern", pattern, NULL);
  return ret;
}

gboolean
check_path (char *path)
{
  FILE *fp = fopen (path, "r");
  if (fp) {
    fclose (fp);
    return TRUE;
  }

  return FALSE;
}

gboolean
check_time (char *time)
{
  static regex_t re;
  static gboolean compiled = FALSE;

  if (!compiled) {
    compiled = TRUE;
    regcomp (&re, "^[0-9]+(.[0-9]+)?$", REG_EXTENDED | REG_NOSUB);
  }

  if (!regexec (&re, time, (size_t) 0, NULL, 0))
    return TRUE;
  return FALSE;
}

guint64
str_to_time (char *time)
{
  if (check_time (time)) {
    return (guint64) (atof (time) * GST_SECOND);
  }
  g_error ("%s not a valid time", time);
  return 0;
}

static GstEncodingProfile *
make_encoding_profile (gchar * audio, gchar * video, gchar * video_restriction,
    gchar * container)
{
  GstEncodingProfile *profile;
  GstStreamEncodingProfile *stream;
  GstCaps *caps;

  caps = gst_caps_from_string (container);
  profile = gst_encoding_profile_new ((gchar *) "ges-test4", caps, NULL, FALSE);
  gst_caps_unref (caps);

  caps = gst_caps_from_string (audio);
  stream =
      gst_stream_encoding_profile_new (GST_ENCODING_PROFILE_AUDIO,
      caps, NULL, NULL, 0);
  gst_encoding_profile_add_stream (profile, stream);
  gst_caps_unref (caps);

  caps = gst_caps_from_string (video);
  stream = gst_stream_encoding_profile_new (GST_ENCODING_PROFILE_VIDEO,
      gst_caps_from_string (video),
      NULL, gst_caps_from_string (video_restriction), 0);
  gst_encoding_profile_add_stream (profile, stream);
  gst_caps_unref (caps);

  return profile;
}

static GESTimelinePipeline *
create_timeline (int nbargs, gchar ** argv)
{
  GESTimelinePipeline *pipeline;
  GESTimelineLayer *layer;
  GESTrack *tracka, *trackv;
  GESTimeline *timeline;
  guint i;

  timeline = ges_timeline_new ();

  tracka = ges_track_audio_raw_new ();
  trackv = ges_track_video_raw_new ();

  /* We are only going to be doing one layer of timeline objects */
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();

  /* Add the tracks and the layer to the timeline */
  if (!ges_timeline_add_layer (timeline, layer) ||
      !ges_timeline_add_track (timeline, tracka) ||
      !ges_timeline_add_track (timeline, trackv))
    return NULL;

  /* Here we've finished initializing our timeline, we're 
   * ready to start using it... by solely working with the layer !*/

  for (i = 0; i < nbargs / 3; i++) {
    GESTimelineObject *obj;

    char *source = argv[i * 3];
    char *arg0 = argv[(i * 3) + 1];
    guint64 duration = str_to_time (argv[(i * 3) + 2]);

    if (!g_strcmp0 ("+pattern", source)) {
      obj = GES_TIMELINE_OBJECT (ges_timeline_test_source_new_for_nick (arg0));
      if (!obj)
        g_error ("%s invalid pattern!", arg0);

      g_object_set (G_OBJECT (obj), "duration", duration, NULL);

      g_print ("Adding <pattern:%s> duration %" GST_TIME_FORMAT "\n",
          arg0, GST_TIME_ARGS (duration));
    }

    else if (!g_strcmp0 ("+transition", source)) {
      obj = GES_TIMELINE_OBJECT (ges_timeline_transition_new_for_nick (arg0));

      if (!obj)
        g_error ("invalid transition type\n");

      if (duration <= 0)
        g_error ("durations must be greater than 0");

      g_object_set (G_OBJECT (obj), "duration", duration, NULL);

      g_print ("Adding <transition:%s> duration %" GST_TIME_FORMAT "\n",
          arg0, GST_TIME_ARGS (duration));

    }

    else if (!g_strcmp0 ("+title", source)) {
      obj = GES_TIMELINE_OBJECT (ges_timeline_title_source_new ());

      g_object_set (obj, "duration", duration, "text", arg0, NULL);

      g_print ("Adding <title:%s> duration %" GST_TIME_FORMAT "\n",
          arg0, GST_TIME_ARGS (duration));
    }

    else {
      gchar *uri;
      guint64 inpoint;

      if (!check_path (source))
        g_error ("'%s': could not open path!", source);

      uri = g_strdup_printf ("file://%s", source);
      inpoint = str_to_time (argv[i * 3 + 1]);

      obj = GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));
      g_object_set (obj,
          "in-point", (guint64) inpoint, "duration", (guint64) duration, NULL);

      g_print ("Adding %s inpoint:%" GST_TIME_FORMAT " duration:%"
          GST_TIME_FORMAT "\n", uri, GST_TIME_ARGS (inpoint),
          GST_TIME_ARGS (duration));

      g_free (uri);

    }

    g_assert (obj);

    /* Since we're using a GESSimpleTimelineLayer, objects will be automatically
     * appended to the end of the layer */
    ges_timeline_layer_add_object (layer, obj);
  }

  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */
  pipeline = ges_timeline_pipeline_new ();

  /* Add the timeline to that pipeline */
  if (!ges_timeline_pipeline_add_timeline (pipeline, timeline))
    return NULL;

  return pipeline;
}

static void
bus_message_cb (GstBus * bus, GstMessage * message, GMainLoop * mainloop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("ERROR\n");
      g_main_loop_quit (mainloop);
      break;
    case GST_MESSAGE_EOS:
      if (repeat > 0) {
        g_print ("Looping again\n");
        /* No need to change state before */
        gst_element_seek_simple (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH, 0);
        gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
        repeat -= 1;
      } else {
        g_print ("Done\n");
        g_main_loop_quit (mainloop);
      }
      break;
    default:
      break;
  }
}

void
print_transition_list (void)
{
  GEnumClass *smpte_class;
  GESTimelineTransition *tr;
  GESTimelineTransitionClass *klass;
  GParamSpec *pspec;
  GEnumValue *v;

  tr = ges_timeline_transition_new (GES_VIDEO_TRANSITION_TYPE_CROSSFADE);
  klass = GES_TIMELINE_TRANSITION_GET_CLASS (tr);

  pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), "vtype");

  smpte_class = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));

  for (v = smpte_class->values; v->value != 0; v++) {
    g_print ("%s\n", v->value_nick);
  }

  g_type_class_unref (smpte_class);
  g_object_unref (tr);
}

void
print_pattern_list (void)
{
  GEnumClass *enum_class;
  GESTimelineTestSource *tr;
  GESTimelineTestSourceClass *klass;
  GParamSpec *pspec;
  GEnumValue *v;

  tr = ges_timeline_test_source_new ();
  klass = GES_TIMELINE_TEST_SOURCE_GET_CLASS (tr);

  pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), "vpattern");

  enum_class = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));

  for (v = enum_class->values; v->value_nick != NULL; v++) {
    g_print ("%s\n", v->value_nick);
  }

  g_type_class_unref (enum_class);
  g_object_unref (tr);

}

int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  gchar *outputuri = NULL;
  gchar *container = (gchar *) "application/ogg";
  gchar *audio = (gchar *) "audio/x-vorbis";
  gchar *video = (gchar *) "video/x-theora";
  gchar *video_restriction = (gchar *) "ANY";
  static gboolean render = FALSE;
  static gboolean smartrender = FALSE;
  static gboolean list_transitions = FALSE;
  static gboolean list_patterns = FALSE;
  static gdouble thumbinterval = 0;
  GOptionEntry options[] = {
    {"thumbnail", 'm', 0.0, G_OPTION_ARG_DOUBLE, &thumbinterval,
        "Take thumbnails every n seconds (saved in current directory)", "N"},
    {"render", 'r', 0, G_OPTION_ARG_NONE, &render,
        "Render to outputuri", NULL},
    {"smartrender", 's', 0, G_OPTION_ARG_NONE, &smartrender,
        "Render to outputuri, and avoid decoding/reencoding", NULL},
    {"outputuri", 'o', 0, G_OPTION_ARG_STRING, &outputuri,
        "URI to encode to", "URI (<protocol>://<location>)"},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &container,
        "Container format", "<GstCaps>"},
    {"vformat", 'v', 0, G_OPTION_ARG_STRING, &video,
        "Video format", "<GstCaps>"},
    {"aformat", 'a', 0, G_OPTION_ARG_STRING, &audio,
        "Audio format", "<GstCaps>"},
    {"vrestriction", 'x', 0, G_OPTION_ARG_STRING, &video_restriction,
        "Video restriction", "<GstCaps>"},
    {"repeat", 'l', 0, G_OPTION_ARG_INT, &repeat,
        "Number of time to repeat timeline", NULL},
    {"list-transitions", 't', 0, G_OPTION_ARG_NONE, &list_transitions,
        "List valid transition types and exit", NULL},
    {"list-patterns", 'p', 0, G_OPTION_ARG_NONE, &list_patterns,
        "List patterns and exit", NULL},
    {NULL}
  };
  GOptionContext *ctx;
  GMainLoop *mainloop;
  GstBus *bus;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("- plays or render a timeline.");
  g_option_context_set_summary (ctx,
      "A timline is a sequence of files, patterns, and transitions.\n"
      "Transitions can only go between patterns or files.\n\n"
      "A file is a tripplet of:\n"
      " * filename\n"
      " * inpoint (in seconds)\n"
      " * duration (in seconds) If 0, full file length\n\n"
      "Patterns and transitions are triplets of:\n"
      " * \"+pattern\" | \"+transition\"\n"
      " * <type>\n" " * duration (in seconds, must be greater than 0)\n");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  if (list_transitions) {
    print_transition_list ();
    exit (0);
  }

  if (list_patterns) {
    print_pattern_list ();
    exit (0);
  }

  if ((argc < 4) || (outputuri && (!render && !smartrender))) {
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    exit (-1);
  }

  g_option_context_free (ctx);
  /* Initialize the GStreamer Editing Services */
  ges_init ();

  /* Create the pipeline */
  pipeline = create_timeline (argc - 1, argv + 1);
  if (!pipeline)
    exit (-1);

  /* Setup profile/encoding if needed */
  if (render || smartrender) {
    GstEncodingProfile *prof;

    prof = make_encoding_profile (audio, video, video_restriction, container);

    if (!prof ||
        !ges_timeline_pipeline_set_render_settings (pipeline, outputuri, prof)
        || !ges_timeline_pipeline_set_mode (pipeline,
            smartrender ? TIMELINE_MODE_SMART_RENDER : TIMELINE_MODE_RENDER))
      exit (-1);

    g_free (outputuri);
    gst_encoding_profile_free (prof);
  } else {
    ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_PREVIEW);
  }

  /* Play the pipeline */
  mainloop = g_main_loop_new (NULL, FALSE);

  if (thumbinterval != 0.0) {
    g_print ("thumbnailing every %f seconds\n", thumbinterval);
    g_timeout_add (1000 * thumbinterval, thumbnail_cb, pipeline);
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), mainloop);

  if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start the encoding\n");
    return 1;
  }
  g_main_loop_run (mainloop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  gst_object_unref (pipeline);

  return 0;
}
