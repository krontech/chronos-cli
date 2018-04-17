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

#include "ioport.h"
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

#define PIPELINE_MODE_LIVE      0   /* Displaying live frame data to the display device. */
#define PIPELINE_MODE_PLAY      1   /* Playing recoded frames back to the display device. */
#define PIPELINE_MODE_PAUSE     2   /* Pause video output during transitions */
#define PIPELINE_MODE_H264      3
#define PIPELINE_MODE_RAW       4
#define PIPELINE_MODE_DNG       5
#define PIPELINE_MODE_PNG       6

#define PIPELINE_IS_RECORDING(_mode_) ((_mode_) > PIPELINE_MODE_PAUSE)

struct pipeline_args {
    unsigned int    mode;
    char            filename[PATH_MAX];
    unsigned long   start;
    unsigned long   length;
    unsigned int    framerate;
    unsigned long   bitrate;
};

struct pipeline_state {
    GMainLoop           *mainloop;
    struct CamVideo     *video;
    struct fpga         *fpga;
    const struct ioport *iops;
    int                 fsync_fd;
    int                 write_fd;

    /* Display control config */
    int                 mode;
    int                 next;
    uint32_t            control;

    /* Video format */
    unsigned long   hres;
    unsigned long   vres;

    /* Playback Mode */
    unsigned long   totalframes;    /* Total number of frames when in playback mode. */
    unsigned long   position;       /* Last played frame number when in playback mode. */
    timer_t         playtimer;      /* Periodic timer - fires to manually play back frames. */
    unsigned int    playrate;       /* Rate (in FPS) of the playback timer. */
    int             playdelta;      /* Change (in frames) to apply at each playback timer expiry. */
    struct playback_region *region_head;
    struct playback_region *region_tail;

    /* Recording Mode */
    unsigned int    preroll;
    unsigned int    estrate;
    struct timespec frametime;

    /* Pipeline args */
    struct pipeline_args args;
};

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
};

struct pipeline_state *cam_pipeline_state(void);

/* Allocate pipeline segments, returning the first pad to be linked. */
GstPad *cam_screencap(struct pipeline_state *state, GstElement *pipeline);
GstPad *cam_lcd_sink(struct pipeline_state *state, GstElement *pipeline, const struct display_config *config);
GstPad *cam_hdmi_sink(struct pipeline_state *state, GstElement *pipeline);
GstPad *cam_h264_sink(struct pipeline_state *state, struct pipeline_args *args, GstElement *pipeline);
GstPad *cam_raw_sink(struct pipeline_state *state, struct pipeline_args *args, GstElement *pipeline);

/* Some background elements. */
void hdmi_hotplug_launch(struct pipeline_state *state);
void dbus_service_launch(struct pipeline_state *state);
void dbus_signal_sof(struct pipeline_state *state);
void dbus_signal_eof(struct pipeline_state *state);

/* Functions for controlling the playback rate. */
void playback_init(struct pipeline_state *state);
void playback_goto(struct pipeline_state *state, unsigned int mode);
void playback_set(struct pipeline_state *state, unsigned long frame, unsigned int rate, int delta);
int playback_region_add(struct pipeline_state *state, unsigned long base, unsigned long size, unsigned long offset);
void playback_region_flush(struct pipeline_state *state);

#endif /* __PIPELINE */
