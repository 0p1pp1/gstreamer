/* GStreamer
 *
 * Copyright (C) 2018-2019 Igalia S.L.
 * Copyright (C) 2018 Metrological Group B.V.
 *  Author: Alicia Boya García <aboya@igalia.com>
 *
 * formatting.c: Functions used by validateflow to get string
 * representations of buffers.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "formatting.h"

#include <gst/gst.h>
#include <string.h>
#include <stdio.h>

typedef void (*Uint64Formatter) (gchar * dest, guint64 time);

void
format_time (gchar * dest_str, guint64 time)
{
  if (GST_CLOCK_TIME_IS_VALID (time)) {
    sprintf (dest_str, "%" GST_TIME_FORMAT, GST_TIME_ARGS (time));
  } else {
    strcpy (dest_str, "none");
  }
}

static void
format_number (gchar * dest_str, guint64 number)
{
  sprintf (dest_str, "%" G_GUINT64_FORMAT, number);
}

gchar *
validate_flow_format_segment (const GstSegment * segment)
{
  Uint64Formatter uint64_format;
  gchar *segment_str;
  gchar *parts[7];
  GString *format;
  gchar start_str[32], offset_str[32], stop_str[32], time_str[32], base_str[32],
      position_str[32], duration_str[32];
  int parts_index = 0;

  uint64_format =
      segment->format == GST_FORMAT_TIME ? format_time : format_number;
  uint64_format (start_str, segment->start);
  uint64_format (offset_str, segment->offset);
  uint64_format (stop_str, segment->stop);
  uint64_format (time_str, segment->time);
  uint64_format (base_str, segment->base);
  uint64_format (position_str, segment->position);
  uint64_format (duration_str, segment->duration);

  format = g_string_new (gst_format_get_name (segment->format));
  format = g_string_ascii_up (format);
  parts[parts_index++] =
      g_strdup_printf ("format=%s, start=%s, offset=%s, stop=%s", format->str,
      start_str, offset_str, stop_str);
  if (segment->rate != 1.0)
    parts[parts_index++] = g_strdup_printf ("rate=%f", segment->rate);
  if (segment->applied_rate != 1.0)
    parts[parts_index++] =
        g_strdup_printf ("applied_rate=%f", segment->applied_rate);
  if (segment->flags)
    parts[parts_index++] = g_strdup_printf ("flags=0x%02x", segment->flags);
  parts[parts_index++] =
      g_strdup_printf ("time=%s, base=%s, position=%s", time_str, base_str,
      position_str);
  if (GST_CLOCK_TIME_IS_VALID (segment->duration))
    parts[parts_index++] = g_strdup_printf ("duration=%s", duration_str);
  parts[parts_index] = NULL;

  segment_str = g_strjoinv (", ", parts);

  while (parts_index > 0)
    g_free (parts[--parts_index]);
  g_string_free (format, TRUE);

  return segment_str;
}

static gboolean
structure_only_given_keys (GQuark field_id, GValue * value,
    gpointer _keys_to_print)
{
  const gchar *const *keys_to_print = (const gchar * const *) _keys_to_print;
  return (!keys_to_print
      || g_strv_contains (keys_to_print, g_quark_to_string (field_id)));
}

static void
gpointer_free (gpointer pointer_location)
{
  g_free (*(void **) pointer_location);
}

gchar *
validate_flow_format_caps (const GstCaps * caps,
    const gchar * const *keys_to_print)
{
  guint i;
  GArray *structures_strv = g_array_new (TRUE, FALSE, sizeof (gchar *));
  gchar *caps_str;

  g_array_set_clear_func (structures_strv, gpointer_free);

  /* A single GstCaps can contain several caps structures (although only one is
   * used in most cases). We will print them separated with spaces. */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure =
        gst_structure_copy (gst_caps_get_structure (caps, i));
    gchar *structure_str;
    gst_structure_filter_and_map_in_place (structure, structure_only_given_keys,
        (gpointer) keys_to_print);
    structure_str = gst_structure_to_string (structure);
    g_array_append_val (structures_strv, structure_str);
  }

  caps_str = g_strjoinv (" ", (gchar **) structures_strv->data);
  g_array_free (structures_strv, TRUE);
  return caps_str;
}


static gchar *
buffer_get_flags_string (GstBuffer * buffer)
{
  GFlagsClass *flags_class =
      G_FLAGS_CLASS (g_type_class_ref (gst_buffer_flags_get_type ()));
  GstBufferFlags flags = GST_BUFFER_FLAGS (buffer);
  GString *string = NULL;

  while (1) {
    GFlagsValue *value = g_flags_get_first_value (flags_class, flags);
    if (!value)
      break;

    if (string == NULL)
      string = g_string_new (NULL);
    else
      g_string_append (string, " ");

    g_string_append (string, value->value_nick);
    flags &= ~value->value;
  }

  return (string != NULL) ? g_string_free (string, FALSE) : NULL;
}

/* Returns a newly-allocated string describing the metas on this buffer, or NULL */
static gchar *
buffer_get_meta_string (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta;
  GString *s = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    const gchar *desc = g_type_name (meta->info->type);

    if (s == NULL)
      s = g_string_new (NULL);
    else
      g_string_append (s, ", ");

    g_string_append (s, desc);
  }

  return (s != NULL) ? g_string_free (s, FALSE) : NULL;
}

gchar *
validate_flow_format_buffer (GstBuffer * buffer)
{
  gchar *flags_str, *meta_str, *buffer_str;
  gchar *buffer_parts[6];
  int buffer_parts_index = 0;

  if (GST_CLOCK_TIME_IS_VALID (buffer->dts)) {
    gchar time_str[32];
    format_time (time_str, buffer->dts);
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("dts=%s", time_str);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer->pts)) {
    gchar time_str[32];
    format_time (time_str, buffer->pts);
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("pts=%s", time_str);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer->duration)) {
    gchar time_str[32];
    format_time (time_str, buffer->duration);
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("dur=%s", time_str);
  }

  flags_str = buffer_get_flags_string (buffer);
  if (flags_str) {
    buffer_parts[buffer_parts_index++] =
        g_strdup_printf ("flags=%s", flags_str);
  }

  meta_str = buffer_get_meta_string (buffer);
  if (meta_str)
    buffer_parts[buffer_parts_index++] = g_strdup_printf ("meta=%s", meta_str);

  buffer_parts[buffer_parts_index] = NULL;
  buffer_str =
      buffer_parts_index > 0 ? g_strjoinv (", ",
      buffer_parts) : g_strdup ("(empty)");

  g_free (meta_str);
  g_free (flags_str);
  while (buffer_parts_index > 0)
    g_free (buffer_parts[--buffer_parts_index]);

  return buffer_str;
}

gchar *
validate_flow_format_event (GstEvent * event, gboolean allow_stream_id,
    const gchar * const *caps_properties)
{
  const gchar *event_type;
  gchar *structure_string;
  gchar *event_string;

  event_type = gst_event_type_get_name (GST_EVENT_TYPE (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    const GstSegment *segment;
    gst_event_parse_segment (event, &segment);
    structure_string = validate_flow_format_segment (segment);
  } else if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstCaps *caps;
    gst_event_parse_caps (event, &caps);
    structure_string = validate_flow_format_caps (caps, caps_properties);
  } else if (!gst_event_get_structure (event)) {
    structure_string = g_strdup ("(no structure)");
  } else {
    GstStructure *printable =
        gst_structure_copy (gst_event_get_structure (event));

    if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START && !allow_stream_id)
      gst_structure_remove_fields (printable, "stream-id", NULL);

    structure_string = gst_structure_to_string (printable);
    gst_structure_free (printable);
  }

  event_string = g_strdup_printf ("%s: %s", event_type, structure_string);
  g_free (structure_string);
  return event_string;
}
