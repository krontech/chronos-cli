/****************************************************************************
 *  Copyright (C) 2018 Kron Technologies Inc <http://www.krontech.ca>.      *
 *                                                                          *
 *  This program is free software: you can redistribute it and/or modify    *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ****************************************************************************/
#ifndef __PIPELINE_H
#define __PIPELINE_H

#include <sys/types.h>
#include <signal.h>
#include <gst/gst.h>

#include "fpga.h"

#define SCREENCAP_PATH      "/tmp/cam-screencap.jpg"

#define LIVE_MAX_FRAMERATE  60

struct pipeline_state {
    GMainLoop       *mainloop;
    struct fpga     *fpga;

    /* Playback Mode */
    timer_t         playtimer;      /* Periodic timer - fires to manually play back frames. */
    int             playrate;       /* Rate (in FPS) of the playback timer. */
    unsigned long   totalframes;    /* Total number of frames when in playback mode. */
    unsigned long   lastframe;      /* Last played frame number when in playback mode. */
    unsigned long   region_size;    /* Playback region size (words) */
    unsigned long   region_base;    /* Playback Region starting address */
    unsigned long   region_first;   /* Playback Region first frame address. */
};

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
};

/* Allocate pipeline segments, returning the first element to be linked. */
GstPad *cam_lcd_sink(GstElement *pipeline, unsigned long hres, unsigned long vres, const struct display_config *config);
GstPad *cam_hdmi_sink(GstElement *pipeline, unsigned long hres, unsigned long vres);
GstPad *cam_screencap(GstElement *pipeline);

/* Some background elements. */
void hdmi_hotplug_launch(struct pipeline_state *state);
void dbus_service_launch(struct pipeline_state *state);

#endif /* __PIPELINE */
