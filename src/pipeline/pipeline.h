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

struct CamVideo;

/* Playback regions are stored as a double-linked list. */
struct playback_region {
    struct playback_region *next;
    struct playback_region *prev;
    unsigned long   size;
    unsigned long   base;
    unsigned long   offset;
    unsigned long   framesz;
};

struct pipeline_state {
    GMainLoop       *mainloop;
    struct CamVideo *video;
    struct fpga     *fpga;

    /* Playback Mode */
    unsigned long   totalframes;    /* Total number of frames when in playback mode. */
    unsigned long   lastframe;      /* Last played frame number when in playback mode. */
    timer_t         playtimer;      /* Periodic timer - fires to manually play back frames. */
    int             playrate;       /* Rate (in FPS) of the playback timer. */
    struct playback_region *region_head;
    struct playback_region *region_tail;

    /* Recording Mode */
    char            filename[PATH_MAX];
    unsigned long   startframe;
    unsigned long   recordlen;
    unsigned int    encoding;
    unsigned int    encrate;
    unsigned long   max_bitrate;
    unsigned long   quality_bpp;
};

/* Video formats that we can encode. */
#define PIPELINE_ENCODE_H264    0
#define PIPELINE_ENCODE_RAW     1
#define PIPELINE_ENCODE_DNG     2
#define PIPELINE_ENCODE_PNG     3

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
};

struct pipeline_state *cam_pipeline_state(void);

/* Allocate pipeline segments, returning the first element to be linked. */
GstPad *cam_lcd_sink(GstElement *pipeline, unsigned long hres, unsigned long vres, const struct display_config *config);
GstPad *cam_hdmi_sink(GstElement *pipeline, unsigned long hres, unsigned long vres);
GstPad *cam_screencap(GstElement *pipeline);

/* Some background elements. */
void hdmi_hotplug_launch(struct pipeline_state *state);
void dbus_service_launch(struct pipeline_state *state);
void dbus_signal_eof(struct pipeline_state *state);

/* Functions for controlling the playback rate. */
void playback_init(struct pipeline_state *state);
void playback_set(struct pipeline_state *state, unsigned long frame, int rate);
int playback_region_add(struct pipeline_state *state, unsigned long base, unsigned long size, unsigned long offset);
void playback_region_flush(struct pipeline_state *state);

#endif /* __PIPELINE */
