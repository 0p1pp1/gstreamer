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
import gst
import gtk
gtk.threads_init()

class DVDPlayer(object):
   def idle(self, pipeline):
      #gtk.threads_enter()
      pipeline.iterate()
      #gtk.threads_leave()
      return 1

   def eof(self, sender):
      print 'EOS, quiting'
      sys.exit(0)
      
   def mpegparse_newpad(self, parser, pad, pipeline):
      #gtk.threads_enter()
      print '***** a new pad %s was created' % pad.get_name()
      if pad.get_name()[:6] == 'video_':
         pad.link(self.v_queue.get_pad('sink'))
         self.pipeline.set_state(gst.STATE_PAUSED)
         self.pipeline.add(self.v_thread)
         #self.v_thread.set_state(gst.STATE_PLAYING)
         self.pipeline.set_state(gst.STATE_PLAYING)
      elif pad.get_name() == 'private_stream_1.0':
         pad.link(self.a_queue.get_pad('sink'))
         self.pipeline.set_state(gst.STATE_PAUSED)
         self.pipeline.add(self.a_thread)
         #self.a_thread.set_state(gst.STATE_PLAYING);
         self.pipeline.set_state(gst.STATE_PLAYING)
      else:
         print 'unknown pad: %s' % pad.get_name()
      #gtk.threads_leave()

   def mpegparse_have_size(self, videosink, width, height):
      gtk.threads_enter()
      self.gtk_socket.set_usize(width,height)
      self.appwindow.show_all()
      gtk.threads_leave()

   def main(self, location, title, chapter, angle):
      self.location = location
      self.title = title
      self.chapter = chapter
      self.angle = angle
      
      #gst_init(&argc,&argv);
      #gnome_init('MPEG2 Video player','0.0.1',argc,argv);

      ret = self.build()
      if ret:
         return ret

      return self.run()

   def run(self):
      print 'setting to PLAYING state'

      gtk.threads_enter()

      self.pipeline.set_state(gst.STATE_PLAYING)

      gtk.idle_add(self.idle,self.pipeline)

      gtk.main()

      self.pipeline.set_state(gst.STATE_NULL)

      gtk.threads_leave()

      return 0

   def build_video_thread(self):
      # ***** pre-construct the video thread *****
      self.v_thread = gst.Thread('v_thread')

      self.v_queue = gst.Element('queue','v_queue')

      self.v_decode = gst.Element('mpeg2dec','decode_video')

      self.color = gst.Element('colorspace','color')

      self.efx = gst.Element('identity','identity')
      #self.efx = gst.Element('edgeTV','EdgeTV')
      #self.efx = gst.Element('agingTV','AgingTV')
      #effectv:  diceTV: DiceTV
      #effectv:  warpTV: WarpTV
      #effectv:  shagadelicTV: ShagadelicTV
      #effectv:  vertigoTV: VertigoTV
      #self.efx = gst.Element('revTV','RevTV')
      #self.efx = gst.Element('quarkTV','QuarkTV')

      self.color2 = gst.Element('colorspace','color2')

      self.show = gst.Element('xvideosink','show')
      #self.show = Element('sdlvideosink','show')
      #self.show = Element('fakesink','fakesinkv')
      #self.show.set_property('silent', 0)
      #self.show.set_property('sync', 1)

      #self.deinterlace = gst.Element('deinterlace','deinterlace')
      self.deinterlace = gst.Element('identity','deinterlace')

      last = None
      for e in (self.v_queue, self.v_decode, self.color, self.efx, self.color2,  self.deinterlace, self.show):
         self.v_thread.add(e)
         if last:
            last.link(e)
         last = e

      #self.v_queue.link(self.v_decode)
      #self.v_decode.link(self.color)
      #self.color.link(self.efx)
      #self.efx.link(self.color2)
      #self.color2.link(self.show)

   def build_audio_thread(self):
      # ***** pre-construct the audio thread *****
      self.a_thread = gst.Thread('a_thread')

      self.a_queue = gst.Element('queue','a_queue')

      self.a_decode = gst.Element('a52dec','decode_audio')

      self.osssink = gst.Element('osssink','osssink')
      #self.osssink = Element('fakesink','fakesinka')
      #self.osssink.set_property('silent', 0)
      #self.osssink.set_property('sync', 0)

      for e in (self.a_queue, self.a_decode, self.osssink):
         self.a_thread.add(e)

      self.a_queue.link(self.a_decode)
      self.a_decode.link(self.osssink)

   def build(self):
      # ***** construct the main pipeline *****
      self.pipeline = gst.Pipeline('pipeline')

      self.src = gst.Element('dvdreadsrc','src');

      self.src.connect('deep_notify',self.dnprint)
      self.src.set_property('location', self.location)
      self.src.set_property('title', self.title)
      self.src.set_property('chapter', self.chapter)
      self.src.set_property('angle', self.angle)

      self.parse = gst.Element('mpegdemux','parse')
      self.parse.set_property('sync', 0)

      self.pipeline.add(self.src)
      self.pipeline.add(self.parse)

      self.src.link(self.parse)

      # pre-construct the audio/video threads
      self.build_video_thread()
      self.build_audio_thread()

      # ***** construct the GUI *****
      #self.appwindow = gnome_app_new('DVD Player','DVD Player')

      #self.gtk_socket = gtk_socket_new ()
      #gtk_socket.show()

      #gnome_app_set_contents(GNOME_APP(appwindow),
            #GTK_WIDGET(gtk_socket));

      #gtk_widget_realize (gtk_socket);
      #gtk_socket_steal (GTK_SOCKET (gtk_socket), 
            #gst_util_get_int_arg (GTK_OBJECT(show), 'xid'));

      self.parse.connect('new_pad',self.mpegparse_newpad, self.pipeline)
      self.src.connect('eos',self.eof)
      #show.connect('have_size',self.mpegparse_have_size, self.pipeline)

      #self.pipeline.connect('error',self.pipeline_error)
      #self.pipeline.connect('deep_notify',self.dnprint)

      return 0

   def pipeline_error(self, sender, obj, error):
      print "(%s) ERROR: %s: %s" % (self, obj.name(), error)

   def dnprint(self, sender, obj, param):
      str = obj.get_property(param.name)
      print '%s: %s = %s' % (sender.get_name(), param.name, str)

def main(args):
   if len(sys.argv) < 5:
      print 'usage: %s dvdlocation title chapter angle' % sys.argv[0]
      return -1

   location = sys.argv[1]
   title = int(sys.argv[2])
   chapter = int(sys.argv[3])
   angle = int(sys.argv[4])

   player = DVDPlayer()
   return player.main(location, title, chapter, angle)
   
if __name__ == '__main__':
   sys.exit(main(sys.argv))
