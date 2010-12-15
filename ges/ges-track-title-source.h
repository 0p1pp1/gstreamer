/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

#ifndef _GES_TRACK_TITLE_SOURCE
#define _GES_TRACK_TITLE_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_TITLE_SOURCE ges_track_title_source_get_type()

#define GES_TRACK_TITLE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_TITLE_SOURCE, GESTrackTitleSource))

#define GES_TRACK_TITLE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_TITLE_SOURCE, GESTrackTitleSourceClass))

#define GES_IS_TRACK_TITLE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_TITLE_SOURCE))

#define GES_IS_TRACK_TITLE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_TITLE_SOURCE))

#define GES_TRACK_TITLE_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_TITLE_SOURCE, GESTrackTitleSourceClass))

typedef struct _GESTrackTitleSourcePrivate GESTrackTitleSourcePrivate;

/** 
 * GESTrackTitleSource:
 *
 */
struct _GESTrackTitleSource {
  GESTrackSource parent;

  /*< private >*/
  gchar         *text;
  gchar         *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
  GstElement    *text_el;
  GstElement    *background_el;

  GESTrackTitleSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTrackTitleSourceClass:
 * @parent_class: parent class
 */

struct _GESTrackTitleSourceClass {
  GESTrackSourceClass parent_class;

  /*< private >*/

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_track_title_source_get_type (void);

void ges_track_title_source_set_text(GESTrackTitleSource *self, const
    gchar *text);

void ges_track_title_source_set_font_desc(GESTrackTitleSource *self,
    const gchar *font_desc);

void ges_track_title_source_set_halignment(GESTrackTitleSource
    *self, GESTextHAlign halgn);

void ges_track_title_source_set_valignment(GESTrackTitleSource
    *self, GESTextVAlign valign);

GESTrackTitleSource* ges_track_title_source_new (void);

G_END_DECLS

#endif /* _GES_TRACK_TITLE_SOURCE */

