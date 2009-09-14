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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-timeline-layer
 * @short_description: Non-overlaping sequence of #GESTimelineObject
 *
 * Responsible for the ordering of the various contained TimelineObject(s)
 */

#include "ges-internal.h"
#include "gesmarshal.h"
#include "ges-timeline-layer.h"
#include "ges.h"

G_DEFINE_TYPE (GESTimelineLayer, ges_timeline_layer, G_TYPE_OBJECT);

enum
{
  OBJECT_ADDED,
  OBJECT_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_layer_signals[LAST_SIGNAL] = { 0 };

static void
ges_timeline_layer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_layer_dispose (GObject * object)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);

  if (layer->objects_start) {
    g_slist_foreach (layer->objects_start, (GFunc) g_object_unref, NULL);
    g_slist_free (layer->objects_start);
    layer->objects_start = NULL;
  }
  G_OBJECT_CLASS (ges_timeline_layer_parent_class)->dispose (object);
}

static void
ges_timeline_layer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_layer_parent_class)->finalize (object);
}

static void
ges_timeline_layer_class_init (GESTimelineLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_timeline_layer_get_property;
  object_class->set_property = ges_timeline_layer_set_property;
  object_class->dispose = ges_timeline_layer_dispose;
  object_class->finalize = ges_timeline_layer_finalize;

  ges_timeline_layer_signals[OBJECT_ADDED] =
      g_signal_new ("object-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass, object_added),
      NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_OBJECT);

  ges_timeline_layer_signals[OBJECT_REMOVED] =
      g_signal_new ("object-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass,
          object_removed), NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_OBJECT);

}

static void
ges_timeline_layer_init (GESTimelineLayer * self)
{
}

GESTimelineLayer *
ges_timeline_layer_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_LAYER, NULL);
}

void
ges_timeline_layer_set_timeline (GESTimelineLayer * layer,
    GESTimeline * timeline)
{
  GST_DEBUG ("layer:%p, timeline:%p", layer, timeline);

  layer->timeline = timeline;
}

static gint
objects_start_compare (GESTimelineObject * a, GESTimelineObject * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    return 0;
  }
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}

/**
 * ges_timeline_layer_add_object:
 * @layer: a #GESTimelineLayer
 * @object: the #GESTimelineObject to add.
 *
 * Adds the object to the layer. The layer will steal a reference to the
 * provided object.
 *
 * Returns: TRUE if the object was properly added to the layer, or FALSE
 * if the @layer refused to add the object.
 */

gboolean
ges_timeline_layer_add_object (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GST_DEBUG ("layer:%p, object:%p", layer, object);

  if (G_UNLIKELY (object->layer)) {
    GST_WARNING ("TimelineObject %p already belongs to another layer");
    return FALSE;
  }

  /* Take a reference to the object and store it stored by start/priority */
  layer->objects_start =
      g_slist_insert_sorted (layer->objects_start, object,
      (GCompareFunc) objects_start_compare);

  /* Inform the object it's now in this layer */
  ges_timeline_object_set_layer (object, layer);

  /* emit 'object-added' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_ADDED], 0, object);

  return TRUE;
}

/**
 * ges_timeline_layer_remove_object:
 * @layer: a #GESTimelineLayer
 * @object: the #GESTimelineObject to remove
 *
 * Removes the given @object from the @layer. The reference stolen by the @layer
 * when the object was added will be removed. If you wish to use the object after
 * this function, make sure you take an extra reference to the object before
 * calling this function.
 *
 * Returns: TRUE if the object was properly remove, else FALSE.
 */
gboolean
ges_timeline_layer_remove_object (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GST_DEBUG ("layer:%p, object:%p", layer, object);

  if (G_UNLIKELY (object->layer != layer)) {
    GST_WARNING ("TimelineObject doesn't belong to this layer");
    return FALSE;
  }

  /* emit 'object-removed' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_REMOVED], 0, object);

  /* inform the object it's no longer in a layer */
  ges_timeline_object_set_layer (object, NULL);

  /* Remove it from our list of controlled objects */
  layer->objects_start = g_slist_remove (layer->objects_start, object);

  /* Remove our reference to the object */
  g_object_unref (object);

  return TRUE;
}
