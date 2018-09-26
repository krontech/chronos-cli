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

#define CAM_LCD_HRES    800
#define CAM_LCD_VRES    480

struct CamVideo;

/* Playback regions are stored as a double-linked list. */
struct playback_region {
    struct playback_region *next;
    struct playback_region *prev;
    /* Size and starting address of the recorded frames. */
    unsigned long   size;
    unsigned long   base;
    unsigned long   offset;
    unsigned long   framesz;
    /* Some frame metadata captured along with the frames. */
    unsigned long   exposure;
    unsigned long   interval;
};

#define PIPELINE_MODE_LIVE      0   /* Displaying live frame data to the display device. */
#define PIPELINE_MODE_PLAY      1   /* Playing recoded frames back to the display device. */
#define PIPELINE_MODE_PAUSE     2   /* Pause video output during transitions */
#define PIPELINE_MODE_H264      3
#define PIPELINE_MODE_RAW16     4   /* 16-bit raw data (padded with zeros LSB) */
#define PIPELINE_MODE_RAW12     5   /* 12-bit packed data */
#define PIPELINE_MODE_DNG       6
#define PIPELINE_MODE_TIFF_GREY 7   /* TIFF/Y16 format */
#define PIPELINE_MODE_TIFF_RGB  8   /* TIFF/RGB888 format */
#define PIPELINE_MODE_BLACKREF  9   /* Save a black reference image. */

#define PIPELINE_IS_SAVING(_mode_) ((_mode_) > PIPELINE_MODE_PAUSE)

struct pipeline_args {
    unsigned int    mode;
    char            filename[PATH_MAX];
    unsigned long   start;
    unsigned long   length;
    unsigned int    framerate;
    unsigned long   bitrate;
};

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
    unsigned char zebra;
    unsigned char peaking;
    unsigned char filter;
};

struct overlay_config {
    unsigned int xoff;
    unsigned int yoff;
    unsigned int width;
    unsigned int height;
    char format[512];
};

#define FRAMERATE_IVAL_BUCKETS  32

#define CAMERA_SERIAL_I2CADDR   0x54
#define CAMERA_SERIAL_LENGTH    32
#define CAMERA_SERIAL_OFFSET    0

struct pipeline_state {
    GMainLoop           *mainloop;
    GstElement          *pipeline;
    GstElement          *source;
    struct CamVideo     *video;
    struct fpga         *fpga;
    const struct ioport *iops;
    int                 fsync_fd;
    int                 write_fd;
    void *              write_buf;
    
    /* Camera information */
    char                serial[CAMERA_SERIAL_LENGTH+1];

    /* Display control config */
    int                 mode;
    int                 next;
    uint32_t            control;

    /* Video format */
    unsigned long   hres;
    unsigned long   vres;

    /* Frame information */
    unsigned long   totalframes;    /* Total number of frames when in playback mode. */
    unsigned long   totalsegs;      /* Total number of recording segments captured. */
    unsigned long   position;       /* Last played frame number when in playback mode. */
    unsigned long   segment;        /* Current segment number */
    unsigned long   segframe;       /* Frame number within the current segment. */
    unsigned long   segsize;        /* Segment size (in frames) */

    /* Playback Mode */
    timer_t         playtimer;      /* Periodic timer - fires to manually play back frames. */
    unsigned int    playrate;       /* Rate (in FPS) of the playback timer. */
    int             playdelta;      /* Change (in frames) to apply at each playback timer expiry. */
    unsigned long   loopstart;      /* Starting frame to play from when in playback mode. */
    unsigned long   loopend;        /* Ending frame to play from when in playback mode. */
    struct playback_region *region_head;
    struct playback_region *region_tail;

    /* Recording Mode */
    unsigned int    phantom;        /* OMX buffering workaround */
    gint            buflevel;       /* OMX buffer level (for frame drop avoidance) */
    unsigned long   dngcount;       /* Frame number for DNG rendering. */
    unsigned int    preroll;        /* Preroll frame counter. */
    void            (*done)(struct pipeline_state *state, const struct pipeline_args *args);

    /* Framerate estimation */
    struct timespec frametime;      /* Timestamp of last frame. */
    unsigned int    frameidx;
    unsigned long   frameival[FRAMERATE_IVAL_BUCKETS]; /* track microseconds between frames. */
    unsigned long long frameisum;   /* Rolling sum of frameival */

    /* Pipeline args */
    struct pipeline_args args;
    struct display_config config;
    struct overlay_config overlay;
};

struct pipeline_state *cam_pipeline_state(void);

/* Pipeline for taking black reference images. */
GstElement *cam_blackref(struct pipeline_state *state, struct pipeline_args *args);

/* Allocate pipeline segments, returning the first pad to be linked. */
GstPad *cam_screencap(struct pipeline_state *state);
GstPad *cam_lcd_sink(struct pipeline_state *state, const struct display_config *config);
void    cam_lcd_reconfig(struct pipeline_state *state, const struct display_config *config);
GstPad *cam_hdmi_sink(struct pipeline_state *state);
GstPad *cam_h264_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_raw_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_dng_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_tiff_sink(struct pipeline_state *state, struct pipeline_args *args);

/* Some background elements. */
void hdmi_hotplug_launch(struct pipeline_state *state);
void dbus_service_launch(struct pipeline_state *state);
void dbus_signal_sof(struct pipeline_state *state);
void dbus_signal_eof(struct pipeline_state *state);

/* Functions for controlling the playback rate. */
void playback_init(struct pipeline_state *state);
void playback_goto(struct pipeline_state *state, unsigned int mode);
void playback_set(struct pipeline_state *state, unsigned long frame, unsigned int rate, int delta);
void playback_loop(struct pipeline_state *state, unsigned long start, unsigned int rate, int delta, unsigned long count);
void playback_region_flush(struct pipeline_state *state);

/* Video overlay control. */
void overlay_clear(struct pipeline_state *state);
void overlay_setup(struct pipeline_state *state);
void overlay_update(struct pipeline_state *state, const struct playback_region *region);

#endif /* __PIPELINE */
