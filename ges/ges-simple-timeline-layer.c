/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:ges-simple-timeline-layer
 * @short_description: High-level #GESTimelineLayer
 *
 * #GESSimpleTimelineLayer allows using #GESTimelineObject(s) with a list-like
 * API. Clients can add any type of GESTimelineObject to a
 * GESSimpleTimelineLayer, and the layer will automatically compute the
 * appropriate start times. 
 *
 * Users should be aware that GESTimelineTransition objects are considered to
 * have a negative duration for the purposes of positioning GESTimelineSource
 * objects (i.e., adding a GESTimelineTransition creates an overlap between
 * the two adjacent sources.
 */

#include "ges-internal.h"
#include "ges-simple-timeline-layer.h"
#include "ges-timeline-object.h"
#include "ges-timeline-source.h"
#include "ges-timeline-transition.h"
#include "gesmarshal.h"

static void
ges_simple_timeline_layer_object_removed (GESTimelineLayer * layer,
    GESTimelineObject * object);

static void
ges_simple_timeline_layer_object_added (GESTimelineLayer * layer,
    GESTimelineObject * object);

static void
timeline_object_height_changed_cb (GESTimelineObject * object G_GNUC_UNUSED,
    GParamSpec * arg G_GNUC_UNUSED, GESSimpleTimelineLayer * layer);

G_DEFINE_TYPE (GESSimpleTimelineLayer, ges_simple_timeline_layer,
    GES_TYPE_TIMELINE_LAYER);

enum
{
  OBJECT_MOVED,
  LAST_SIGNAL,
};

static guint gstl_signals[LAST_SIGNAL] = { 0 };

static void
ges_simple_timeline_layer_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_timeline_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_timeline_layer_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_simple_timeline_layer_parent_class)->dispose (object);
}

static void
ges_simple_timeline_layer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_simple_timeline_layer_parent_class)->finalize (object);
}

static void
ges_simple_timeline_layer_class_init (GESSimpleTimelineLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineLayerClass *layer_class = GES_TIMELINE_LAYER_CLASS (klass);

  object_class->get_property = ges_simple_timeline_layer_get_property;
  object_class->set_property = ges_simple_timeline_layer_set_property;
  object_class->dispose = ges_simple_timeline_layer_dispose;
  object_class->finalize = ges_simple_timeline_layer_finalize;

  /* Be informed when objects are being added/removed from elsewhere */
  layer_class->object_removed = ges_simple_timeline_layer_object_removed;
  layer_class->object_added = ges_simple_timeline_layer_object_added;

  /**
   * GESSimpleTimelineLayer::object-moved
   * @layer: the #GESSimpleTimelineLayer
   * @object: the #GESTimelineObject that was added
   * @old: the previous position of the object
   * @new: the new position of the object
   *
   * Will be emitted when an object is moved with
   * #ges_simple_timeline_layer_move_object.
   */
  gstl_signals[OBJECT_MOVED] =
      g_signal_new ("object-moved", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESSimpleTimelineLayerClass,
          object_moved),
      NULL, NULL,
      ges_marshal_VOID__OBJECT_INT_INT, G_TYPE_NONE, 3,
      GES_TYPE_TIMELINE_OBJECT, G_TYPE_INT, G_TYPE_INT);
}

static void
ges_simple_timeline_layer_init (GESSimpleTimelineLayer * self)
{
  self->objects = NULL;
}

static void
gstl_recalculate (GESSimpleTimelineLayer * self)
{
  GList *tmp;
  GstClockTime pos = 0;
  gint priority = 0;
  gint transition_priority = 0;
  gint height;
  GESTimelineObject *prev_object = NULL;
  GESTimelineObject *prev_transition = NULL;

  priority = GES_TIMELINE_LAYER (self)->min_gnl_priority + 2;

  GST_DEBUG ("recalculating values");

  for (tmp = self->objects; tmp; tmp = tmp->next) {
    GESTimelineObject *obj;
    guint64 dur;
    GList *l_next;

    obj = (GESTimelineObject *) tmp->data;
    dur = GES_TIMELINE_OBJECT_DURATION (obj);
    height = GES_TIMELINE_OBJECT_HEIGHT (obj);

    if (GES_IS_TIMELINE_SOURCE (obj)) {

      GST_LOG ("%p obj: height: %d: priority %d", obj, height, priority);

      if (G_UNLIKELY (GES_TIMELINE_OBJECT_START (obj) != pos)) {
        ges_timeline_object_set_start (obj, pos);
      }

      if (G_UNLIKELY (GES_TIMELINE_OBJECT_PRIORITY (obj) != priority)) {
        ges_timeline_object_set_priority (obj, priority);
      }

      transition_priority = MAX (0, priority - 1);
      priority += height;
      pos += dur;

      g_assert (priority != -1);

    } else if (GES_IS_TIMELINE_TRANSITION (obj)) {

      pos -= dur;

      GST_LOG ("%p obj: height: %d: trans_priority %d", obj, height,
          transition_priority);

      g_assert (transition_priority != -1);

      if (G_UNLIKELY (GES_TIMELINE_OBJECT_START (obj) != pos))
        ges_timeline_object_set_start (obj, pos);

      if (G_UNLIKELY (GES_TIMELINE_OBJECT_PRIORITY (obj) !=
              transition_priority)) {
        ges_timeline_object_set_priority (obj, transition_priority);
      }

      /* sanity checks */
      l_next = g_list_next (tmp);

      if (GES_IS_TIMELINE_TRANSITION (prev_object)) {
        GST_ERROR ("two transitions in sequence!");
      }

      if (prev_object && (GES_TIMELINE_OBJECT_DURATION (prev_object) < dur)) {
        GST_ERROR ("transition duration exceeds that of previous neighbor!");
      }

      if (l_next && (GES_TIMELINE_OBJECT_DURATION (l_next->data) < dur)) {
        GST_ERROR ("transition duration exceeds that of next neighbor!");
      }

      if (prev_transition) {
        guint64 start, end;
        end = (GES_TIMELINE_OBJECT_DURATION (prev_transition) +
            GES_TIMELINE_OBJECT_START (prev_transition));

        start = pos;

        if (end > start)
          GST_ERROR ("%d, %d: overlapping transitions!", start, end);
      }
      prev_transition = obj;
    }

    prev_object = obj;

  }

  GST_DEBUG ("Finished recalculating: final start pos is: " GST_TIME_FORMAT,
      GST_TIME_ARGS (pos));

  GES_TIMELINE_LAYER (self)->max_gnl_priority = priority;
}

/**
 * ges_simple_timeline_layer_add_object:
 * @layer: a #GESSimpleTimelineLayer
 * @object: the #GESTimelineObject to add
 * @position: the position at which to add the object
 *
 * Adds the object at the given position in the layer. The position is where
 * the object will be inserted. To put the object before all objects, use
 * position 0. To put after all objects, use position -1.
 * 
 * When adding transitions, it is important that the adjacent objects
 * (objects at position, and position + 1) be (1) A derivative of
 * GESTimelineSource or other non-transition, and (2) have a duration at least
 * as long as the duration of the transition.
 *
 * The layer will steal a reference to the provided object.
 *
 * Returns: TRUE if the object was successfuly added, else FALSE.
 */
gboolean
ges_simple_timeline_layer_add_object (GESSimpleTimelineLayer * layer,
    GESTimelineObject * object, gint position)
{
  gboolean res;
  GList *nth;

  GST_DEBUG ("layer:%p, object:%p, position:%d", layer, object, position);

  nth = g_list_nth (layer->objects, position);

  if (GES_IS_TIMELINE_TRANSITION (object)) {
    GList *lprev = g_list_previous (nth);

    GESTimelineObject *prev = GES_TIMELINE_OBJECT (lprev ? lprev->data : NULL);
    GESTimelineObject *next = GES_TIMELINE_OBJECT (nth ? nth->data : NULL);

    if ((prev && GES_IS_TIMELINE_TRANSITION (prev)) ||
        (next && GES_IS_TIMELINE_TRANSITION (next))) {
      GST_ERROR ("Not adding transition: Only insert transitions between two"
          " sources, or at the begining or end the layer\n");
      return FALSE;
    }
  }


  layer->adding_object = TRUE;

  res = ges_timeline_layer_add_object ((GESTimelineLayer *) layer, object);

  /* Add to layer */
  if (G_UNLIKELY (!res)) {
    layer->adding_object = FALSE;
    return FALSE;
  }

  layer->adding_object = FALSE;

  GST_DEBUG ("Adding object %p to the list", object);

  layer->objects = g_list_insert (layer->objects, object, position);

  g_signal_connect (G_OBJECT (object), "notify::height", G_CALLBACK
      (timeline_object_height_changed_cb), layer);

  /* recalculate positions */
  gstl_recalculate (layer);

  return TRUE;
}

/**
 * ges_simple_timeline_layer_move_object:
 * @layer: a #GESSimpleTimelineLayer
 * @object: the #GESTimelineObject to move
 * @newposition: the new position at which to move the object
 *
 * Moves the object to the given position in the layer. To put the object before
 * all other objects, use position 0. To put the objects after all objects, use
 * position -1.
 *
 * Returns: TRUE if the object was successfuly moved, else FALSE.
 */

gboolean
ges_simple_timeline_layer_move_object (GESSimpleTimelineLayer * layer,
    GESTimelineObject * object, gint newposition)
{
  gint idx;

  GST_DEBUG ("layer:%p, object:%p, newposition:%d", layer, object, newposition);

  if (G_UNLIKELY (object->layer != (GESTimelineLayer *) layer)) {
    GST_WARNING ("TimelineObject doesn't belong to this layer");
    return FALSE;
  }

  /* Find it's current position */
  idx = g_list_index (layer->objects, object);
  if (G_UNLIKELY (idx == -1)) {
    GST_WARNING ("TimelineObject not controlled by this layer");
    return FALSE;
  }

  GST_DEBUG ("Object was previously at position %d", idx);

  /* If we don't have to change its position, don't */
  if (idx == newposition)
    return TRUE;

  /* pop it off the list */
  layer->objects = g_list_remove (layer->objects, object);

  newposition = (newposition >= 0 && newposition < idx) ? newposition :
      newposition - 1;

  /* re-add it at the proper position */
  layer->objects = g_list_insert (layer->objects, object, newposition);

  /* recalculate positions */
  gstl_recalculate (layer);

  g_signal_emit (layer, gstl_signals[OBJECT_MOVED], 0, object, idx,
      newposition);

  return TRUE;
}

/**
 * ges_simple_timeline_layer_new:
 *
 * Creates a new #GESSimpleTimelineLayer.
 *
 * Returns: The new #GESSimpleTimelineLayer
 */
GESSimpleTimelineLayer *
ges_simple_timeline_layer_new (void)
{
  return g_object_new (GES_TYPE_SIMPLE_TIMELINE_LAYER, NULL);
}

static void
ges_simple_timeline_layer_object_removed (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GESSimpleTimelineLayer *sl = (GESSimpleTimelineLayer *) layer;

  /* remove object from our list */
  sl->objects = g_list_remove (sl->objects, object);
  gstl_recalculate (sl);
}

static void
ges_simple_timeline_layer_object_added (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GESSimpleTimelineLayer *sl = (GESSimpleTimelineLayer *) layer;

  if (sl->adding_object == FALSE) {
    /* remove object from our list */
    sl->objects = g_list_append (sl->objects, object);
    gstl_recalculate (sl);
  }
  g_signal_connect_swapped (object, "notify::duration",
      G_CALLBACK (gstl_recalculate), layer);
}

static void
timeline_object_height_changed_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, GESSimpleTimelineLayer * layer)
{
  GST_LOG ("layer %p: notify height changed %p", layer, object);
  gstl_recalculate (layer);
}
