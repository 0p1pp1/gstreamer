#!/usr/bin/env python
#
# gst-python
# Copyright (C) 2002 David I. Lehn
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import sys
from gstreamer import *
from gobject import GObject

def update(sender, *args):
   print sender.get_name(), args

def filter(filters):
   "A GStreamer copy pipeline which can add arbitrary filters"

   if len(sys.argv) != 3:
      print 'usage: %s source dest' % (sys.argv[0])
      return -1

   # create a new bin to hold the elements
   bin = Pipeline('pipeline')

   filesrc = gst_element_factory_make('filesrc', 'source');
   if not filesrc:
      print 'could not find plugin \"filesrc\"'
      return -1
   filesrc.set_property('location', sys.argv[1])

   filesink = gst_element_factory_make('filesink', 'sink')
   if not filesink:
      print 'could not find plugin \"filesink\"'
      return -1
   filesink.set_property('location', sys.argv[2])

   elements = [filesrc] + filters + [filesink]
   #  add objects to the main pipeline
   for e in elements: 
      bin.add(e)

   # connect the elements
   previous = None
   for e in elements:
      if previous:
         previous.connect(e)
      previous = e

   # start playing
   bin.set_state(STATE_PLAYING);

   while bin.iterate(): pass

   # stop the bin
   bin.set_state(STATE_NULL)

   return 0

def main():
   "A GStreamer based cp(1) with stats"
   #gst_info_set_categories(-1)
   #gst_debug_set_categories(-1)

   stats = gst_element_factory_make ('statistics', 'stats');
   if not stats:
      print 'could not find plugin \"statistics\"'
      return -1
   stats.set_property('silent', 0)
   stats.set_property('buffer_update_freq', 1)
   stats.set_property('update_on_eos', 1)
   #GObject.connect(stats, 'update', update)

   return filter([stats])

if __name__ == '__main__':
   ret = main()
   sys.exit (ret)
