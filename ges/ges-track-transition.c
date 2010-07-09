/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon@collabora.co.uk>
 *               2010 Nokia Corporation
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

/** 
 * SECTION:ges-track-transition
 * @short_description: Concrete, track-level implemenation of audio and video
 * transitinos.
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-transition.h"
#include "ges-timeline-transition.h"

G_DEFINE_TYPE (GESTrackTransition, ges_track_transition, GES_TYPE_TRACK_OBJECT);

GstElement *ges_track_transition_create_element (GESTrackTransition * self,
    GESTrack * track);

static void
ges_track_transition_update_vcontroller (GESTrackTransition * self,
    GstElement * gnlobj)
{
  GValue start_value = { 0, };
  GValue end_value = { 0, };
  guint64 duration;

  GST_LOG ("updating controller");

  if (!gnlobj)
    return;

  if (!(self->vcontroller))
    return;

  GST_LOG ("getting properties");
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %d\n", duration);
  g_value_init (&start_value, G_TYPE_DOUBLE);
  g_value_init (&end_value, G_TYPE_DOUBLE);
  g_value_set_double (&start_value, self->vstart_value);
  g_value_set_double (&end_value, self->vend_value);

  GST_LOG ("setting values on controller");

  g_assert (GST_IS_CONTROLLER (self->vcontroller));
  g_assert (GST_IS_CONTROL_SOURCE (self->vcontrol_source));

  gst_interpolation_control_source_unset_all (self->vcontrol_source);
  gst_interpolation_control_source_set (self->vcontrol_source, 0, &start_value);
  gst_interpolation_control_source_set (self->vcontrol_source,
      duration, &end_value);

  GST_LOG ("done updating controller");
}

static void
ges_track_transition_update_acontroller (GESTrackTransition * self,
    GstElement * gnlobj)
{
  guint64 duration;
  GValue zero = { 0, };
  GValue one = { 0, };

  GST_LOG ("updating controller: gnlobj (%p) acontroller(%p) bcontroller(%p)",
      gnlobj, self->a_acontroller, self->a_bcontroller);

  if (!gnlobj)
    return;

  if (!(self->a_acontroller) || !(self->a_bcontroller))
    return;

  GST_LOG ("getting properties");
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %lud\n", duration);
  g_value_init (&zero, G_TYPE_DOUBLE);
  g_value_init (&one, G_TYPE_DOUBLE);
  g_value_set_double (&zero, 0.0);
  g_value_set_double (&one, 1.0);

  GST_LOG ("setting values on controller");

  g_assert (GST_IS_CONTROLLER (self->a_acontroller));
  g_assert (GST_IS_CONTROL_SOURCE (self->a_acontrol_source));

  g_assert (GST_IS_CONTROLLER (self->a_bcontroller));
  g_assert (GST_IS_CONTROL_SOURCE (self->a_bcontrol_source));

  gst_interpolation_control_source_unset_all (self->a_acontrol_source);
  gst_interpolation_control_source_set (self->a_acontrol_source, 0, &one);
  gst_interpolation_control_source_set (self->a_acontrol_source,
      duration, &zero);

  gst_interpolation_control_source_unset_all (self->a_bcontrol_source);
  gst_interpolation_control_source_set (self->a_bcontrol_source, 0, &zero);
  gst_interpolation_control_source_set (self->a_bcontrol_source,
      duration, &one);

  GST_LOG ("done updating controller");
}

static void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackTransition * self)
{
  GESTrackType type = ((GESTrackObject *) self)->track->type;
  GST_LOG ("got duration changed signal");

  if (type == GES_TRACK_TYPE_VIDEO)
    ges_track_transition_update_vcontroller (self, gnlobject);
  else if (type == GES_TRACK_TYPE_AUDIO) {
    GST_LOG ("transition is an audio transition");
    ges_track_transition_update_acontroller (self, gnlobject);
  }
}

static void
ges_track_transition_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_transition_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_transition_dispose (GObject * object)
{
  GESTrackTransition *self = GES_TRACK_TRANSITION (object);

  GST_DEBUG ("disposing");
  GST_LOG ("mixer: %p smpte: %p sinka: %p sinkb: %p",
      self->vmixer, self->vsmpte, self->sinka, self->sinkb);

  if (self->vcontroller) {
    g_object_unref (self->vcontroller);
    self->vcontroller = NULL;
    if (self->vcontrol_source)
      gst_object_unref (self->vcontrol_source);
    self->vcontrol_source = NULL;
  }

  if (self->a_acontroller) {
    g_object_unref (self->a_acontroller);
    self->a_acontroller = NULL;
    if (self->a_acontrol_source)
      gst_object_unref (self->a_acontrol_source);
    self->a_acontrol_source = NULL;
  }

  if (self->a_bcontroller) {
    g_object_unref (self->a_bcontroller);
    self->a_bcontroller = NULL;
    if (self->a_bcontrol_source)
      gst_object_unref (self->a_bcontrol_source);
    self->a_bcontrol_source = NULL;
  }

  if (self->vmixer && self->sinka && self->sinkb) {
    GST_DEBUG ("releasing request pads for vmixer");
    gst_element_release_request_pad (self->vmixer, self->sinka);
    gst_element_release_request_pad (self->vmixer, self->sinkb);
    gst_object_unref (self->vmixer);
    gst_object_unref (self->sinka);
    gst_object_unref (self->sinkb);
    self->vmixer = NULL;
    self->sinka = NULL;
    self->sinkb = NULL;
  }

  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

static void
ges_track_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_transition_parent_class)->finalize (object);
}

static GObject *
link_element_to_mixer (GstElement * element, GstElement * mixer)
{
  GstPad *sinkpad = gst_element_get_request_pad (mixer, "sink_%d");
  GstPad *srcpad = gst_element_get_static_pad (element, "src");

  g_assert (sinkpad);
  g_assert (srcpad);

  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);

  return G_OBJECT (sinkpad);
}

static GObject *
link_element_to_mixer_with_smpte (GstBin * bin, GstElement * element,
    GstElement * mixer, gint type, GstElement ** smpteref)
{
  GstElement *smptealpha = gst_element_factory_make ("smptealpha", NULL);
  g_object_set (G_OBJECT (smptealpha),
      "type", (gint) type, "invert", (gboolean) TRUE, NULL);
  gst_bin_add (bin, smptealpha);

  gst_element_link_many (element, smptealpha, mixer, NULL);

  /* crack */
  if (smpteref) {
    *smpteref = smptealpha;
  }

  return G_OBJECT (smptealpha);
}

static GObject *
link_element_to_mixer_with_volume (GstBin * bin, GstElement * element,
    GstElement * mixer)
{
  GstElement *volume = gst_element_factory_make ("volume", NULL);
  gst_bin_add (bin, volume);

  gst_element_link_many (element, volume, mixer, NULL);

  return G_OBJECT (volume);
}

static GstElement *
create_video_bin (GESTrackTransition * self)
{
  GstElement *topbin, *iconva, *iconvb, *oconv;
  GObject *target = NULL;
  const gchar *propname = NULL;
  GstElement *mixer = NULL;
  GstPad *sinka_target, *sinkb_target, *src_target, *sinka, *sinkb, *src;
  GstController *controller;
  GstInterpolationControlSource *control_source;

  GST_LOG ("creating a video bin");

  topbin = gst_bin_new ("transition-bin");
  iconva = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-a");
  iconvb = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-b");
  oconv = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);
  mixer = gst_element_factory_make ("videomixer", NULL);
  g_object_set (G_OBJECT (mixer), "background", 1, NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  if (self->vtype != VTYPE_CROSSFADE) {
    link_element_to_mixer_with_smpte (GST_BIN (topbin), iconva, mixer,
        self->vtype, NULL);
    target = link_element_to_mixer_with_smpte (GST_BIN (topbin), iconvb,
        mixer, self->vtype, &self->vsmpte);
    propname = "position";
    self->vstart_value = 1.0;
    self->vend_value = 0.0;
  } else {
    self->sinka = (GstPad *) link_element_to_mixer (iconva, mixer);
    self->sinkb = (GstPad *) link_element_to_mixer (iconvb, mixer);
    target = (GObject *) self->sinkb;
    self->vmixer = gst_object_ref (mixer);
    propname = "alpha";
    self->vstart_value = 0.0;
    self->vend_value = 1.0;
  }

  gst_element_link (mixer, oconv);

  sinka_target = gst_element_get_static_pad (iconva, "sink");
  sinkb_target = gst_element_get_static_pad (iconvb, "sink");
  src_target = gst_element_get_static_pad (oconv, "src");

  sinka = gst_ghost_pad_new ("sinka", sinka_target);
  sinkb = gst_ghost_pad_new ("sinkb", sinkb_target);
  src = gst_ghost_pad_new ("src", src_target);

  gst_element_add_pad (topbin, src);
  gst_element_add_pad (topbin, sinka);
  gst_element_add_pad (topbin, sinkb);

  gst_object_unref (sinka_target);
  gst_object_unref (sinkb_target);
  gst_object_unref (src_target);

  /* set up interpolation */

  g_object_set (target, propname, (gfloat) 0.0, NULL);

  controller = gst_object_control_properties (target, propname, NULL);

  control_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (controller,
      propname, GST_CONTROL_SOURCE (control_source));
  gst_interpolation_control_source_set_interpolation_mode (control_source,
      GST_INTERPOLATE_LINEAR);

  self->vcontroller = controller;
  self->vcontrol_source = control_source;

  GST_LOG ("controller created, updating");
  ges_track_transition_update_vcontroller (self,
      ((GESTrackObject *) self)->gnlobject);

  return topbin;
}

static GstElement *
create_audio_bin (GESTrackTransition * self)
{
  GstElement *topbin, *iconva, *iconvb, *oconv;
  GObject *atarget, *btarget = NULL;
  const gchar *propname = "volume";
  GstElement *mixer = NULL;
  GstPad *sinka_target, *sinkb_target, *src_target, *sinka, *sinkb, *src;
  GstController *acontroller, *bcontroller;
  GstInterpolationControlSource *acontrol_source, *bcontrol_source;


  GST_LOG ("creating an audio bin");

  topbin = gst_bin_new ("transition-bin");
  iconva = gst_element_factory_make ("audioconvert", "tr-aconv-a");
  iconvb = gst_element_factory_make ("audioconvert", "tr-aconv-b");
  oconv = gst_element_factory_make ("audioconvert", "tr-aconv-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);

  mixer = gst_element_factory_make ("adder", NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  atarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconva, mixer);
  btarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconvb, mixer);

  g_assert (atarget && btarget);

  gst_element_link (mixer, oconv);

  sinka_target = gst_element_get_static_pad (iconva, "sink");
  sinkb_target = gst_element_get_static_pad (iconvb, "sink");
  src_target = gst_element_get_static_pad (oconv, "src");

  sinka = gst_ghost_pad_new ("sinka", sinka_target);
  sinkb = gst_ghost_pad_new ("sinkb", sinkb_target);
  src = gst_ghost_pad_new ("src", src_target);

  gst_element_add_pad (topbin, src);
  gst_element_add_pad (topbin, sinka);
  gst_element_add_pad (topbin, sinkb);

  /* set up interpolation */

  gst_object_unref (sinka_target);
  gst_object_unref (sinkb_target);
  gst_object_unref (src_target);


  //g_object_set(atarget, propname, (gdouble) 0, NULL);
  //g_object_set(btarget, propname, (gdouble) 0, NULL);

  acontroller = gst_object_control_properties (atarget, propname, NULL);
  bcontroller = gst_object_control_properties (btarget, propname, NULL);

  g_assert (acontroller && bcontroller);

  acontrol_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (acontroller,
      propname, GST_CONTROL_SOURCE (acontrol_source));
  gst_interpolation_control_source_set_interpolation_mode (acontrol_source,
      GST_INTERPOLATE_LINEAR);

  bcontrol_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (bcontroller,
      propname, GST_CONTROL_SOURCE (bcontrol_source));
  gst_interpolation_control_source_set_interpolation_mode (bcontrol_source,
      GST_INTERPOLATE_LINEAR);

  self->a_acontroller = acontroller;
  self->a_bcontroller = bcontroller;
  self->a_acontrol_source = acontrol_source;
  self->a_bcontrol_source = bcontrol_source;

  GST_LOG ("controllers created, updating");

  ges_track_transition_update_acontroller (self,
      ((GESTrackObject *) self)->gnlobject);

  return topbin;
}

static gboolean
ges_track_transition_create_gnl_object (GESTrackObject * object)
{
  GESTrackTransition *self;
  GESTrackTransitionClass *klass;
  GstElement *element;
  gchar *name;
  static gint tnum = 0;

  self = GES_TRACK_TRANSITION (object);
  klass = GES_TRACK_TRANSITION_GET_CLASS (object);

  name = g_strdup_printf ("transition-operation%d", tnum++);
  object->gnlobject = gst_element_factory_make ("gnloperation", name);
  g_free (name);

  g_object_set (object->gnlobject, "priority", 0, NULL);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::duration",
      G_CALLBACK (gnlobject_duration_cb), object);

  element = klass->create_element (self, object->track);
  if (!GST_IS_ELEMENT (element))
    return FALSE;

  gst_bin_add (GST_BIN (object->gnlobject), element);
  return TRUE;
}

GstElement *
ges_track_transition_create_element (GESTrackTransition * self,
    GESTrack * track)
{
  if ((track->type) == GES_TRACK_TYPE_VIDEO) {
    return create_video_bin (self);
  }

  else if ((track->type) == GES_TRACK_TYPE_AUDIO) {
    return create_audio_bin (self);
  }

  return gst_element_factory_make ("identity", "invalid-track-type");
}

static void
ges_track_transition_class_init (GESTrackTransitionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_transition_get_property;
  object_class->set_property = ges_track_transition_set_property;
  object_class->dispose = ges_track_transition_dispose;
  object_class->finalize = ges_track_transition_finalize;

  track_class->create_gnl_object = ges_track_transition_create_gnl_object;
  klass->create_element = ges_track_transition_create_element;
}

static void
ges_track_transition_init (GESTrackTransition * self)
{
  self->vcontroller = NULL;
  self->vcontrol_source = NULL;
  self->vsmpte = NULL;
  self->vmixer = NULL;
  self->sinka = NULL;
  self->sinkb = NULL;
  self->vtype = 0;
  self->vstart_value = 0.0;
  self->vend_value = 0.0;

  self->a_acontroller = NULL;
  self->a_acontrol_source = NULL;

  self->a_bcontroller = NULL;
  self->a_bcontrol_source = NULL;
}

void
ges_track_transition_set_vtype (GESTrackTransition * self, gint vtype)
{
  if (((vtype == VTYPE_CROSSFADE) && (self->vtype != VTYPE_CROSSFADE)) ||
      ((vtype != VTYPE_CROSSFADE) && (self->vtype = VTYPE_CROSSFADE))) {
    GST_WARNING
        ("Changing between 'crossfade' and other types is not supported\n");
  }

  self->vtype = vtype;
  if (self->vsmpte && (vtype != VTYPE_CROSSFADE))
    g_object_set (self->vsmpte, "type", (gint) vtype, NULL);
}

GESTrackTransition *
ges_track_transition_new (gint value)
{
  GESTrackTransition *ret = g_object_new (GES_TYPE_TRACK_TRANSITION, NULL);
  ret->vtype = value;

  return ret;
}
