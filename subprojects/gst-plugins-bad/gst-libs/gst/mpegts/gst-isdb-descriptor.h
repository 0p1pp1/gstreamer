/*
 * gst-isdb-descriptor.h -
 * Copyright (C) 2020 Edward Hervey
 *
 * Authors:
 *   Edward Hervey <edward@centricular.com>
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

#ifndef GST_ISDB_DESCRIPTOR_H
#define GST_ISDB_DESCRIPTOR_H

#include <gst/gst.h>
#include <gst/mpegts/mpegts-prelude.h>

G_BEGIN_DECLS

/**
 * SECTION:gst-isdb-descriptor
 * @title: ISDB variants of MPEG-TS descriptors
 * @short_description: Descriptors for the various ISDB specifications
 * @include: gst/mpegts/mpegts.h
 *
 * This contains the various descriptors defined by the ISDB specifications
 */

/**
 * GstMpegtsISDBDescriptorType:
 *
 * These values correspond to the registered descriptor type from
 * the various ISDB specifications.
 *
 * Consult the relevant specifications for more details.
 */
typedef enum {
  /* ISDB ARIB B10 v4.6 */
  GST_MTS_DESC_ISDB_HIERARCHICAL_TRANSMISSION   = 0xC0,
  GST_MTS_DESC_ISDB_DIGITAL_COPY_CONTROL        = 0xC1,
  GST_MTS_DESC_ISDB_NETWORK_IDENTIFICATION      = 0xC2,
  GST_MTS_DESC_ISDB_PARTIAL_TS_TIME             = 0xc3,
  GST_MTS_DESC_ISDB_AUDIO_COMPONENT             = 0xc4,
  GST_MTS_DESC_ISDB_HYPERLINK                   = 0xc5,
  GST_MTS_DESC_ISDB_TARGET_REGION               = 0xc6,
  GST_MTS_DESC_ISDB_DATA_CONTENT                = 0xc7,
  GST_MTS_DESC_ISDB_VIDEO_DECODE_CONTROL        = 0xc8,
  GST_MTS_DESC_ISDB_DOWNLOAD_CONTENT            = 0xc9,
  GST_MTS_DESC_ISDB_CA_EMM_TS                   = 0xca,
  GST_MTS_DESC_ISDB_CA_CONTRACT_INFORMATION     = 0xcb,
  GST_MTS_DESC_ISDB_CA_SERVICE                  = 0xcc,
  GST_MTS_DESC_ISDB_TS_INFORMATION              = 0xcd,
  GST_MTS_DESC_ISDB_EXTENDED_BROADCASTER        = 0xce,
  GST_MTS_DESC_ISDB_LOGO_TRANSMISSION           = 0xcf,
  GST_MTS_DESC_ISDB_BASIC_LOCAL_EVENT           = 0xd0,
  GST_MTS_DESC_ISDB_REFERENCE                   = 0xd1,
  GST_MTS_DESC_ISDB_NODE_RELATION               = 0xd2,
  GST_MTS_DESC_ISDB_SHORT_NODE_INFORMATION      = 0xd3,
  GST_MTS_DESC_ISDB_STC_REFERENCE               = 0xd4,
  GST_MTS_DESC_ISDB_SERIES                      = 0xd5,
  GST_MTS_DESC_ISDB_EVENT_GROUP                 = 0xd6,
  GST_MTS_DESC_ISDB_SI_PARAMETER                = 0xd7,
  GST_MTS_DESC_ISDB_BROADCASTER_NAME            = 0xd8,
  GST_MTS_DESC_ISDB_COMPONENT_GROUP             = 0xd9,
  GST_MTS_DESC_ISDB_SI_PRIME_TS                 = 0xda,
  GST_MTS_DESC_ISDB_BOARD_INFORMATION           = 0xdb,
  GST_MTS_DESC_ISDB_LDT_LINKAGE                 = 0xdc,
  GST_MTS_DESC_ISDB_CONNECTED_TRANSMISSION      = 0xdd,
  GST_MTS_DESC_ISDB_CONTENT_AVAILABILITY        = 0xde,
  /* ... */
  GST_MTS_DESC_ISDB_SERVICE_GROUP               = 0xe0

} GstMpegtsISDBDescriptorType;

/* GST_MTS_ISDB_DESC_SERIES (0xD5) */

typedef struct _GstMpegtsIsdbEventSeries GstMpegtsIsdbEventSeries;

typedef enum {
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_IRREGULAR = 0,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_SLOT,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_WEEKLY,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_MONTHLY,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_LUMPED,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_SPLIT,

} GstMpegtsIsdbProgramPattern;

/**
 * GstMpegtsIsdbEventSeries:
 * @repeat_label: if %0, this event is in the 1st run of the series.
 * otherwise, identifies the re-run/repeated series of the same series,
 * overlapping /running concurrently in the same time period with the 1st run.
 * @expire_date: the expiry date of this series. NULL if undecided.
 *
 * ISDB Event Series Descriptor
 * (ARIB STD B10 v5.8 Part2 6.2.33, ARIB TR B14 v6.2 Fascicle4 18)
 */
struct _GstMpegtsIsdbEventSeries {
  guint16                     series_id;
  guint8                      repeat_label;
  GstMpegtsIsdbProgramPattern program_pattern;
  GstDateTime                 *expire_date;
  guint16                     episode_number;
  guint16                     last_episode_number;
  gchar                       *series_name;
};

#define GST_TYPE_MPEGTS_ISDB_EVENT_SERIES (gst_mpegts_isdb_event_series_get_type())

GST_MPEGTS_API
GType gst_mpegts_isdb_event_series_get_type (void);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_series (const GstMpegtsDescriptor *descriptor,
                                             GstMpegtsIsdbEventSeries **res);


/* GST_MTS_ISDB_DESC_EVENT_GROUP (0xD6) */
typedef struct _GstMpegtsIsdbEventGroupDescriptor GstMpegtsIsdbEventGroupDescriptor;
typedef struct _GstMpegtsIsdbEventRef GstMpegtsIsdbEventRef;

typedef enum {
  GST_MPEGTS_EVENT_GROUP_TYPE_SHARED = 1,
  GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO_INTERNAL,
  GST_MPEGTS_EVENT_GROUP_TYPE_MOVED_FROM_INTERNAL,
  GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO,
  GST_MPEGTS_EVENT_GROUP_TYPE_MOVED_FROM,
} GstMpegtsEventGroupType;

struct _GstMpegtsIsdbEventRef {
  guint16 original_network_id;   /* defined only for group_type >= 4 */
  guint16 transport_stream_id;   /* defined only for group_type >= 4 */
  guint16 service_id;
  guint16 event_id;
};

#define GST_TYPE_MPEGTS_ISDB_EVENT_REF (gst_mpegts_isdb_event_ref_get_type())

GST_MPEGTS_API
GType gst_mpegts_isdb_event_ref_get_type (void);

/**
 * GstMpegtsIsdbEventGroupDescriptor:
 * @gtype:
 * @events: (element-type GstMpegtsIsdbEventRef): the #GstMpegtsIsdbEventRef
 *
 * ISDB Event Group Descriptor.
 * (ARIB STD B10 v5.8 Part2 6.2.34, ARIB TR B14 v6.2 Fascicle4 17, 19, 24)
 */
struct _GstMpegtsIsdbEventGroupDescriptor {
  GstMpegtsEventGroupType group_type;
  GPtrArray *events;
};

#define GST_TYPE_MPEGTS_ISDB_EVENT_GROUP_DESCRIPTOR (gst_mpegts_isdb_event_group_descriptor_get_type())

GST_MPEGTS_API
GType gst_mpegts_isdb_event_group_descriptor_get_type (void);

GST_MPEGTS_API
void gst_mpegts_isdb_event_group_descriptor_free (GstMpegtsIsdbEventGroupDescriptor *source);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_event_group (const GstMpegtsDescriptor *descriptor,
    GstMpegtsIsdbEventGroupDescriptor **res);

G_END_DECLS

#endif
