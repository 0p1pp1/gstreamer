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

import pygtk
pygtk.require('2.0')
import sys
import os
import dl

"libtool lib location"
devloc = os.path.join(__path__[0],'.libs')

if os.path.exists(devloc):
   sys.path.append(devloc)

sys.setdlopenflags(dl.RTLD_LAZY | dl.RTLD_GLOBAL)
del devloc, sys, os

from _gstreamer import *

#from gtk import threads_init, threads_enter, threads_leave

def threads_init():
    import gtk
    gtk.threads_init()
