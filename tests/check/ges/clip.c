/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "test-utils.h"
#include "../../../ges/ges-structured-interface.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

GST_START_TEST (test_object_properties)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;

  ges_init ();

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));

  clip = (GESClip *) ges_test_clip_new ();
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (clip, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (clip), 420);
  assert_equals_uint64 (_DURATION (clip), 510);
  assert_equals_uint64 (_INPOINT (clip), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);


  /* This time, we move the trackelement to see if the changes move
   * along to the parent and the gnonlin clip */
  g_object_set (trackelement, "start", (guint64) 400, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 400);
  assert_equals_uint64 (_START (trackelement), 400);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 400, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_split_direct_bindings)
{
  GList *values;
  GstControlSource *source;
  GESTimeline *timeline;
  GESClip *clip, *splitclip;
  GstControlBinding *binding = NULL, *splitbinding;
  GstTimedValueControlSource *splitsource;
  GESLayer *layer;
  GESAsset *asset;
  GValue *tmpvalue;

  GESTrackElement *element;

  ges_init ();

  fail_unless ((timeline = ges_timeline_new ()));
  fail_unless ((layer = ges_layer_new ()));
  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  clip = ges_layer_add_asset (layer, asset, 0, 10 * GST_SECOND, 10 * GST_SECOND,
      GES_TRACK_TYPE_UNKNOWN);
  g_object_unref (asset);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 10 * GST_SECOND);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  check_layer (clip, 0);

  source = gst_interpolation_control_source_new ();
  g_object_set (source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
  element = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (ges_track_element_set_control_source (element,
          source, "alpha", "direct"));

  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      10 * GST_SECOND, 0.0);
  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      20 * GST_SECOND, 1.0);

  binding = ges_track_element_get_control_binding (element, "alpha");
  tmpvalue = gst_control_binding_get_value (binding, 10 * GST_SECOND);
  assert_equals_int (g_value_get_double (tmpvalue), 0.0);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  tmpvalue = gst_control_binding_get_value (binding, 20 * GST_SECOND);
  assert_equals_int (g_value_get_double (tmpvalue), 1.0);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  splitclip = ges_clip_split (clip, 5 * GST_SECOND);
  CHECK_OBJECT_PROPS (splitclip, 5 * GST_SECOND, 15 * GST_SECOND,
      5 * GST_SECOND);
  check_layer (splitclip, 0);

  splitbinding =
      ges_track_element_get_control_binding (GES_CONTAINER_CHILDREN
      (splitclip)->data, "alpha");
  g_object_get (splitbinding, "control_source", &splitsource, NULL);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (splitsource));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 0.5);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      20 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 1.0);
  g_list_free (values);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (source));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      10 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 0.0);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 0.50);
  g_list_free (values);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 5 * GST_SECOND);
  check_layer (clip, 0);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_split_direct_absolute_bindings)
{
  GList *values;
  GstControlSource *source;
  GESTimeline *timeline;
  GESClip *clip, *splitclip;
  GstControlBinding *binding = NULL, *splitbinding;
  GstTimedValueControlSource *splitsource;
  GESLayer *layer;
  GESAsset *asset;
  GValue *tmpvalue;

  GESTrackElement *element;

  ges_init ();

  fail_unless ((timeline = ges_timeline_new ()));
  fail_unless ((layer = ges_layer_new ()));
  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  clip = ges_layer_add_asset (layer, asset, 0, 10 * GST_SECOND, 10 * GST_SECOND,
      GES_TRACK_TYPE_UNKNOWN);
  g_object_unref (asset);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 10 * GST_SECOND);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  check_layer (clip, 0);

  source = gst_interpolation_control_source_new ();
  g_object_set (source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
  element = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (ges_track_element_set_control_source (element,
          source, "posx", "direct-absolute"));

  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      10 * GST_SECOND, 0);
  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      20 * GST_SECOND, 500);

  binding = ges_track_element_get_control_binding (element, "posx");
  tmpvalue = gst_control_binding_get_value (binding, 10 * GST_SECOND);
  assert_equals_int (g_value_get_int (tmpvalue), 0);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  tmpvalue = gst_control_binding_get_value (binding, 20 * GST_SECOND);
  assert_equals_int (g_value_get_int (tmpvalue), 500);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  splitclip = ges_clip_split (clip, 5 * GST_SECOND);
  CHECK_OBJECT_PROPS (splitclip, 5 * GST_SECOND, 15 * GST_SECOND,
      5 * GST_SECOND);
  check_layer (splitclip, 0);

  splitbinding =
      ges_track_element_get_control_binding (GES_CONTAINER_CHILDREN
      (splitclip)->data, "posx");
  g_object_get (splitbinding, "control_source", &splitsource, NULL);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (splitsource));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 250.0);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      20 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 500.0);
  g_list_free (values);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (source));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      10 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 0.0);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 250.0);
  g_list_free (values);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 5 * GST_SECOND);
  check_layer (clip, 0);

  gst_object_unref (timeline);
  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_split_object)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip, *splitclip;
  GList *splittrackelements;
  GESTrackElement *trackelement, *splittrackelement;

  ges_init ();

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  clip = GES_CLIP (ges_test_clip_new ());
  fail_unless (clip != NULL);
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 50,
      "in-point", (guint64) 12, NULL);
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 50);
  assert_equals_uint64 (_INPOINT (clip), 12);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 2);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 50);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 50, 12,
      50, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  splitclip = ges_clip_split (clip, 67);
  fail_unless (GES_IS_CLIP (splitclip));

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 25);
  assert_equals_uint64 (_INPOINT (clip), 12);

  assert_equals_uint64 (_START (splitclip), 67);
  assert_equals_uint64 (_DURATION (splitclip), 25);
  assert_equals_uint64 (_INPOINT (splitclip), 37);

  splittrackelements = GES_CONTAINER_CHILDREN (splitclip);
  fail_unless_equals_int (g_list_length (splittrackelements), 2);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  assert_equals_uint64 (_START (splittrackelement), 67);
  assert_equals_uint64 (_DURATION (splittrackelement), 25);
  assert_equals_uint64 (_INPOINT (splittrackelement), 37);

  fail_unless (splittrackelement != trackelement);
  fail_unless (splitclip != clip);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->next->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  assert_equals_uint64 (_START (splittrackelement), 67);
  assert_equals_uint64 (_DURATION (splittrackelement), 25);
  assert_equals_uint64 (_INPOINT (splittrackelement), 37);

  fail_unless (splittrackelement != trackelement);
  fail_unless (splitclip != clip);

  /* We own the only ref */
  ASSERT_OBJECT_REFCOUNT (splitclip, "1 ref for us + 1 for the timeline", 2);
  /* 1 ref for the Clip, 1 ref for the Track and 2 ref for the timeline
   * (1 for the "all_element" hashtable, another for the sequence of TrackElement*/
  ASSERT_OBJECT_REFCOUNT (splittrackelement,
      "1 ref for the Clip, 1 ref for the Track and 1 ref for the timeline", 3);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (splitclip), clip,
      splittrackelement, NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_clip_group_ungroup)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GESClip *clip, *clip2;
  GList *containers, *tmp;
  GESLayer *layer;
  GESContainer *regrouped_clip;
  GESTrack *audio_track, *video_track;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  audio_track = GES_TRACK (ges_audio_track_new ());
  video_track = GES_TRACK (ges_video_track_new ());

  fail_unless (ges_timeline_add_track (timeline, audio_track));
  fail_unless (ges_timeline_add_track (timeline, video_track));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  assert_is_type (asset, GES_TYPE_ASSET);

  clip = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  ASSERT_OBJECT_REFCOUNT (clip, "1 layer + 1 timeline.all_elements", 2);
  assert_equals_uint64 (_START (clip), 0);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 10);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 2);

  containers = ges_container_ungroup (GES_CONTAINER (clip), FALSE);
  assert_equals_int (g_list_length (containers), 2);
  fail_unless (clip == containers->data);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  assert_equals_uint64 (_START (clip), 0);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 10);
  ASSERT_OBJECT_REFCOUNT (clip, "1 for the layer + 1 for the timeline + "
      "1 in containers list", 3);

  clip2 = containers->next->data;
  fail_if (clip2 == clip);
  fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (clip2) != NULL);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip2)), 1);
  assert_equals_uint64 (_START (clip2), 0);
  assert_equals_uint64 (_INPOINT (clip2), 0);
  assert_equals_uint64 (_DURATION (clip2), 10);
  ASSERT_OBJECT_REFCOUNT (clip2, "1 for the layer + 1 for the timeline +"
      " 1 in containers list", 3);

  tmp = ges_track_get_elements (audio_track);
  assert_equals_int (g_list_length (tmp), 1);
  ASSERT_OBJECT_REFCOUNT (tmp->data, "1 for the track + 1 for the container "
      "+ 1 for the timeline + 1 in tmp list", 4);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_AUDIO);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP
          (ges_timeline_element_get_parent (tmp->data))), GES_TRACK_TYPE_AUDIO);
  g_list_free_full (tmp, gst_object_unref);
  tmp = ges_track_get_elements (video_track);
  assert_equals_int (g_list_length (tmp), 1);
  ASSERT_OBJECT_REFCOUNT (tmp->data, "1 for the track + 1 for the container "
      "+ 1 for the timeline + 1 in tmp list", 4);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_VIDEO);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP
          (ges_timeline_element_get_parent (tmp->data))), GES_TRACK_TYPE_VIDEO);
  g_list_free_full (tmp, gst_object_unref);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), 10);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  assert_equals_uint64 (_START (clip), 10);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 10);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip2)), 1);
  assert_equals_uint64 (_START (clip2), 0);
  assert_equals_uint64 (_INPOINT (clip2), 0);
  assert_equals_uint64 (_DURATION (clip2), 10);

  regrouped_clip = ges_container_group (containers);
  fail_unless (GES_IS_GROUP (regrouped_clip));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (regrouped_clip)),
      2);
  tmp = ges_container_ungroup (regrouped_clip, FALSE);
  g_list_free_full (tmp, gst_object_unref);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), 0);
  regrouped_clip = ges_container_group (containers);
  assert_is_type (regrouped_clip, GES_TYPE_CLIP);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (regrouped_clip)),
      2);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP (regrouped_clip)),
      GES_TRACK_TYPE_VIDEO | GES_TRACK_TYPE_AUDIO);
  g_list_free_full (containers, gst_object_unref);

  GST_DEBUG ("Check clips in the layer");
  tmp = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (tmp), 1);
  g_list_free_full (tmp, gst_object_unref);

  GST_DEBUG ("Check TrackElement in audio track");
  tmp = ges_track_get_elements (audio_track);
  assert_equals_int (g_list_length (tmp), 1);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_AUDIO);
  fail_unless (GES_CONTAINER (ges_timeline_element_get_parent (tmp->data)) ==
      regrouped_clip);
  g_list_free_full (tmp, gst_object_unref);

  GST_DEBUG ("Check TrackElement in video track");
  tmp = ges_track_get_elements (video_track);
  assert_equals_int (g_list_length (tmp), 1);
  ASSERT_OBJECT_REFCOUNT (tmp->data, "1 for the track + 1 for the container "
      "+ 1 for the timeline + 1 in tmp list", 4);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_VIDEO);
  fail_unless (GES_CONTAINER (ges_timeline_element_get_parent (tmp->data)) ==
      regrouped_clip);
  g_list_free_full (tmp, gst_object_unref);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;


static void
child_removed_cb (GESClip * clip, GESTimelineElement * effect,
    gboolean * called)
{
  ASSERT_OBJECT_REFCOUNT (effect, "Keeping alive ref + emission ref", 2);
  *called = TRUE;
}

GST_START_TEST (test_clip_refcount_remove_child)
{
  GESClip *clip;
  GESTrack *track;
  gboolean called;
  GESTrackElement *effect;

  ges_init ();

  clip = GES_CLIP (ges_test_clip_new ());
  track = GES_TRACK (ges_audio_track_new ());
  effect = GES_TRACK_ELEMENT (ges_effect_new ("identity"));

  fail_unless (ges_track_add_element (track, effect));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container + 1 for the track", 2);

  fail_unless (ges_track_remove_element (track, effect));
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container + 1 for the track", 1);

  g_signal_connect (clip, "child-removed", G_CALLBACK (child_removed_cb),
      &called);
  fail_unless (ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (called == TRUE);

  check_destroyed (G_OBJECT (track), NULL, NULL);
  check_destroyed (G_OBJECT (clip), NULL, NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_clip_find_track_element)
{
  GESClip *clip;
  GList *foundelements;
  GESTimeline *timeline;
  GESTrack *track, *track1, *track2;

  GESTrackElement *effect, *effect1, *effect2, *foundelem;

  ges_init ();

  clip = GES_CLIP (ges_test_clip_new ());
  track = GES_TRACK (ges_audio_track_new ());
  track1 = GES_TRACK (ges_audio_track_new ());
  track2 = GES_TRACK (ges_video_track_new ());

  timeline = ges_timeline_new ();
  fail_unless (ges_timeline_add_track (timeline, track));
  fail_unless (ges_timeline_add_track (timeline, track1));
  fail_unless (ges_timeline_add_track (timeline, track2));

  effect = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track, effect));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track1, effect1));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect1)));

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track2, effect2));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect2)));

  foundelem = ges_clip_find_track_element (clip, track, G_TYPE_NONE);
  fail_unless (foundelem == effect);
  gst_object_unref (foundelem);

  foundelem = ges_clip_find_track_element (clip, NULL, GES_TYPE_SOURCE);
  fail_unless (foundelem == NULL);

  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_AUDIO, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 2);
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_VIDEO, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 1);
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, track,
      GES_TRACK_TYPE_VIDEO, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 2);
  fail_unless (g_list_find (foundelements, effect2) != NULL,
      "In the video track");
  fail_unless (g_list_find (foundelements, effect2) != NULL, "In 'track'");
  g_list_free_full (foundelements, gst_object_unref);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_effects_priorities)
{
  GESClip *clip;
  GESTimeline *timeline;
  GESTrack *audio_track, *video_track;
  GESLayer *layer, *layer1;

  GESTrackElement *effect, *effect1, *effect2;

  ges_init ();

  clip = GES_CLIP (ges_test_clip_new ());
  audio_track = GES_TRACK (ges_audio_track_new ());
  video_track = GES_TRACK (ges_video_track_new ());

  timeline = ges_timeline_new ();
  fail_unless (ges_timeline_add_track (timeline, audio_track));
  fail_unless (ges_timeline_add_track (timeline, video_track));

  layer = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);

  ges_layer_add_clip (layer, clip);

  effect = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect1)));

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect2)));

  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          2));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect1));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect2));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          0));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  fail_unless (ges_clip_move_to_layer (clip, layer1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          2));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect2));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          0));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static void
_count_cb (GObject * obj, GParamSpec * pspec, gint * count)
{
  *count = *count + 1;
}

#define _assert_children_time_setter(clip, child, prop, setter, val1, val2) \
{ \
  gint clip_count = 0; \
  gint child_count = 0; \
  gchar *notify_name = g_strconcat ("notify::", prop, NULL); \
  gchar *clip_name = GES_TIMELINE_ELEMENT_NAME (clip); \
  gchar *child_name = NULL; \
  g_signal_connect (clip, notify_name, G_CALLBACK (_count_cb), \
      &clip_count); \
  if (child) { \
    child_name = GES_TIMELINE_ELEMENT_NAME (child); \
    g_signal_connect (child, notify_name, G_CALLBACK (_count_cb), \
        &child_count); \
  } \
  \
  fail_unless (setter (GES_TIMELINE_ELEMENT (clip), val1), \
      "Failed to set the %s property for clip %s", prop, clip_name); \
  assert_clip_children_time_val (clip, prop, val1); \
  \
  fail_unless (clip_count == 1, "The callback for the %s property was " \
      "called %i times for clip %s, rather than once", \
      prop, clip_count, clip_name); \
  if (child) { \
    fail_unless (child_count == 1, "The callback for the %s property " \
        "was called %i times for the child %s of clip %s, rather than " \
        "once", prop, child_count, child_name, clip_name); \
  } \
  \
  clip_count = 0; \
  if (child) { \
    child_count = 0; \
    fail_unless (setter (GES_TIMELINE_ELEMENT (child), val2), \
        "Failed to set the %s property for the child %s of clip %s", \
        prop, child_name, clip_name); \
    fail_unless (child_count == 1, "The callback for the %s property " \
        "was called %i more times for the child %s of clip %s, rather " \
        "than once more", prop, child_count, child_name, clip_name); \
  } else { \
    fail_unless (setter (GES_TIMELINE_ELEMENT (clip), val2), \
      "Failed to set the %s property for clip %s", prop, clip_name); \
  } \
  assert_clip_children_time_val (clip, prop, val2); \
  \
  fail_unless (clip_count == 1, "The callback for the %s property " \
      "was called %i more times for clip %s, rather than once more", \
      prop, clip_count, clip_name); \
  assert_equals_int (g_signal_handlers_disconnect_by_func (clip, \
          G_CALLBACK (_count_cb), &clip_count), 1); \
  if (child) { \
    assert_equals_int (g_signal_handlers_disconnect_by_func (child, \
            G_CALLBACK (_count_cb), &child_count), 1); \
  } \
  \
  g_free (notify_name); \
}

static void
_test_children_time_setting_on_clip (GESClip * clip, GESTimelineElement * child)
{
  /* FIXME: Don't necessarily want to change the inpoint of all the
   * children if the clip inpoint changes. Really, we would only expect
   * the inpoint to change for the source elements within a clip.
   * Setting the inpoint of an operation may be irrelevant, and for
   * operations where it *is* relevant, we would ideally want it to be
   * independent from the source element's inpoint (unlike the start and
   * duration values).
   * However, this is the current behaviour, but if this is changed this
   * test should be changed to only check that source elements have
   * their inpoint changed, and operation elements have their inpoint
   * unchanged */
  _assert_children_time_setter (clip, child, "in-point",
      ges_timeline_element_set_inpoint, 11, 101);
  _assert_children_time_setter (clip, child, "in-point",
      ges_timeline_element_set_inpoint, 51, 1);
  _assert_children_time_setter (clip, child, "start",
      ges_timeline_element_set_start, 12, 102);
  _assert_children_time_setter (clip, child, "start",
      ges_timeline_element_set_start, 52, 2);
  _assert_children_time_setter (clip, child, "duration",
      ges_timeline_element_set_duration, 13, 103);
  _assert_children_time_setter (clip, child, "duration",
      ges_timeline_element_set_duration, 53, 3);
}

GST_START_TEST (test_children_time_setters)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clips[] = { NULL, NULL };
  gint i;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline);

  layer = ges_timeline_append_layer (timeline);

  clips[0] =
      GES_CLIP (ges_transition_clip_new
      (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE));
  clips[1] = GES_CLIP (ges_test_clip_new ());

  for (i = 0; i < G_N_ELEMENTS (clips); i++) {
    GESClip *clip = clips[i];
    GESContainer *group = GES_CONTAINER (ges_group_new ());
    GList *children;
    GESTimelineElement *child;
    /* no children */
    _test_children_time_setting_on_clip (clip, NULL);
    /* child in timeline */
    fail_unless (ges_layer_add_clip (layer, clip));
    children = GES_CONTAINER_CHILDREN (clip);
    fail_unless (children);
    child = GES_TIMELINE_ELEMENT (children->data);
    _test_children_time_setting_on_clip (clip, child);
    /* clip in a group */
    ges_container_add (group, GES_TIMELINE_ELEMENT (clip));
    _test_children_time_setting_on_clip (clip, child);
    /* group is removed from the timeline and destroyed when empty */
    ges_container_remove (group, GES_TIMELINE_ELEMENT (clip));
    /* child not in timeline */
    gst_object_ref (clip);
    fail_unless (ges_layer_remove_clip (layer, clip));
    children = GES_CONTAINER_CHILDREN (clip);
    fail_unless (children);
    child = GES_TIMELINE_ELEMENT (children->data);
    _test_children_time_setting_on_clip (clip, child);
    gst_object_unref (clip);
  }
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_can_add_effect)
{
  struct CanAddEffectData
  {
    GESClip *clip;
    gboolean can_add_effect;
  } clips[6];
  guint i;
  gchar *uri;

  ges_init ();

  uri = ges_test_get_audio_video_uri ();

  clips[0] = (struct CanAddEffectData) {
  GES_CLIP (ges_test_clip_new ()), TRUE};
  clips[1] = (struct CanAddEffectData) {
  GES_CLIP (ges_uri_clip_new (uri)), TRUE};
  clips[2] = (struct CanAddEffectData) {
  GES_CLIP (ges_title_clip_new ()), TRUE};
  clips[3] = (struct CanAddEffectData) {
  GES_CLIP (ges_effect_clip_new ("agingtv", "audioecho")), TRUE};
  clips[4] = (struct CanAddEffectData) {
  GES_CLIP (ges_transition_clip_new
        (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE)), FALSE};
  clips[5] = (struct CanAddEffectData) {
  GES_CLIP (ges_text_overlay_clip_new ()), FALSE};

  g_free (uri);

  for (i = 0; i < G_N_ELEMENTS (clips); i++) {
    GESClip *clip = clips[i].clip;
    GESTimelineElement *effect =
        GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
    gst_object_ref_sink (effect);
    gst_object_ref_sink (clip);
    fail_unless (clip);
    if (clips[i].can_add_effect)
      fail_unless (ges_container_add (GES_CONTAINER (clip), effect),
          "Could not add an effect to clip %s",
          GES_TIMELINE_ELEMENT_NAME (clip));
    else
      fail_if (ges_container_add (GES_CONTAINER (clip), effect),
          "Could add an effect to clip %s, but we expect this to fail",
          GES_TIMELINE_ELEMENT_NAME (clip));
    gst_object_unref (effect);
    gst_object_unref (clip);
  }

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-clip");
  TCase *tc_chain = tcase_create ("clip");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_object_properties);
  tcase_add_test (tc_chain, test_split_object);
  tcase_add_test (tc_chain, test_split_direct_bindings);
  tcase_add_test (tc_chain, test_split_direct_absolute_bindings);
  tcase_add_test (tc_chain, test_clip_group_ungroup);
  tcase_add_test (tc_chain, test_clip_refcount_remove_child);
  tcase_add_test (tc_chain, test_clip_find_track_element);
  tcase_add_test (tc_chain, test_effects_priorities);
  tcase_add_test (tc_chain, test_children_time_setters);
  tcase_add_test (tc_chain, test_can_add_effect);

  return s;
}

GST_CHECK_MAIN (ges);
