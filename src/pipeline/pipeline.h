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
#include "segment.h"
#include "fpga.h"

#define SCREENCAP_PATH      "/tmp/cam-screencap.jpg"

#define LIVE_MAX_FRAMERATE  60
#define SAVE_MAX_FRAMERATE  230

#define CAM_LCD_HRES    800
#define CAM_LCD_VRES    480

#define PIPELINE_MAX_HRES   1920
#define PIPELINE_MAX_VRES   1080
#define PIPELINE_SCRATCHPAD_SIZE (PIPELINE_MAX_HRES * PIPELINE_MAX_VRES * 4)

#define NETWORK_STREAM_PORT 5000

struct CamVideo;
struct rtsp_ctx;

/* Only enable RTSP for sufficiently "new" versions of GStreamer. */
#define ENABLE_RTSP_SERVER  GST_CHECK_VERSION(0,10,36)

/* RTSP Session Information */
#define RTSP_SESSION_TEARDOWN   0
#define RTSP_SESSION_SETUP      1
#define RTSP_SESSION_PLAY       2
#define RTSP_SESSION_PAUSE      3

struct rtsp_session {
    char    host[64];
    int     port;
    int     state;
};
typedef void (*rtsp_session_hook_t)(const struct rtsp_session *sess, void *closure);

#define PLAYBACK_STATE_PAUSE    0   /* Paused - no video output. */
#define PLAYBACK_STATE_LIVE     1   /* Live display of video from the image sensor. */
#define PLAYBACK_STATE_PLAY     2   /* Playback of recorded frames from memory. */
#define PLAYBACK_STATE_FILESAVE 3   /* Encoding video data into a file. */

#define PIPELINE_MODE_PAUSE     0   /* Paused - no video output. */
#define PIPELINE_MODE_LIVE      1   /* Live display of video from the image sensor. */
#define PIPELINE_MODE_PLAY      2   /* Playback of recorded frames from memory. */
#define PIPELINE_MODE_H264      3
#define PIPELINE_MODE_RAW16     4   /* 16-bit raw data (padded with zeros LSB) */
#define PIPELINE_MODE_RAW12     5   /* 12-bit packed data */
#define PIPELINE_MODE_DNG       6
#define PIPELINE_MODE_TIFF      7   /* Processed 8-bit TIFF format. */
#define PIPELINE_MODE_TIFF_RAW  8   /* Linear RAW 16-bit TIFF format. */

#define PIPELINE_IS_SAVING(_mode_) ((_mode_) > PIPELINE_MODE_PLAY)

struct pipeline_args {
    unsigned int    mode;
    char            filename[PATH_MAX];
    unsigned long   start;
    unsigned long   length;
    unsigned int    framerate;
    unsigned long   bitrate;
};

struct source_config {
    unsigned int    color;
    unsigned int    hres;
    unsigned int    vres;
    unsigned int    cropx;
    unsigned int    cropy;
    unsigned int    startx;
    unsigned int    starty;
    unsigned int    flip;
    unsigned int    rate;
};

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
    unsigned int  peak_color;  /* One of DISPLAY_CTL_FOCUS_PEAK_xxx or zero to disable. */
    double        peak_level;  /* In the range of 0.0 for minimum sensitivity to 1.0 for max. */
    double        zebra_level; /* Exposure zebra sensitivity level. */
    const char *gifsplash;
};

struct overlay_config {
    unsigned char enable;
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

#define PIPELINE_ERROR_MAXLEN   80

struct pipeline_state {
    pthread_t           mainthread;
    GMainContext        *mainctx;
    GMainLoop           *mainloop;
    GstElement          *pipeline;
    GstElement          *vidsrc;
    GstEvent            *eos;
    struct CamVideo     *video;
    struct rtsp_ctx     *rtsp;
    struct fpga         *fpga;
    const struct ioport *iops;
    int                 runmode;
    int                 write_fd;
    void *              scratchpad;
    char                error[PIPELINE_ERROR_MAXLEN];
    
    /* Camera information */
    char                serial[CAMERA_SERIAL_LENGTH+1];

    /* Display control config */
    uint32_t            control;

    /* Destination address for UDP/network stream. */
    char            nethost[128];

    /* Frame information */
    struct video_seglist seglist;   /* List of segments captured from the recording sequencer. */
    pthread_mutex_t segmutex;       /* Lock access to the segment list. */
    long            position;       /* Last played frame number, or negative for live display. */

    /* Playback Mode */
    int             playstate;      /* Playback state machine. */
    long            playrate;       /* Playback rate in frames per second. */
    long            playcounter;    /* Internal counter for framerate control. */
    unsigned long   playstart;      /* Starting frame to play from when in playback mode. */
    unsigned long   playlength;     /* Length of video to play from when in playback mode. */
    unsigned int    playloop;       /* Loop playback or return to live display. */
    pthread_t       playthread;     /* Thread handle for the playback frame manager. */

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

    /* Pipeline config */
    struct pipeline_args args;
    struct source_config source;
    struct display_config config;
    struct overlay_config overlay;
};

struct pipeline_state *cam_pipeline_state(void);
void cam_pipeline_restart(struct pipeline_state *state);

/* Allocate pipeline segments, returning the first pad to be linked. */
GstPad *cam_screencap(struct pipeline_state *state);
GstPad *cam_lcd_sink(struct pipeline_state *state, const struct display_config *config);
void    cam_lcd_reconfig(struct pipeline_state *state, const struct display_config *config);
GstPad *cam_hdmi_sink(struct pipeline_state *state);
GstPad *cam_h264_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_network_sink(struct pipeline_state *state);
GstPad *cam_raw_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_dng_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_tiff_sink(struct pipeline_state *state, struct pipeline_args *args);
GstPad *cam_tiffraw_sink(struct pipeline_state *state, struct pipeline_args *args);

/* Some background elements. */
struct CamVideo *dbus_service_launch(struct pipeline_state *state);
void dbus_service_cleanup(struct CamVideo *video);
void dbus_signal_sof(struct CamVideo *video);
void dbus_signal_eof(struct CamVideo *video, const char *err);
void dbus_signal_segment(struct CamVideo *video);
void dbus_signal_update(struct CamVideo *video, const char **names);
gboolean dbus_get_param(struct pipeline_state *state, const char *name, GHashTable *data);
gboolean dbus_set_param(struct pipeline_state *state, const char *name, GValue *gval, char *err);
GHashTable *dbus_describe_params(struct pipeline_state *state);

/* HDMI Hotplug watcher needs to be in its own thread. */
void hdmi_hotplug_launch(struct pipeline_state *state);

/* Functions for controlling the playback rate. */
void playback_init(struct pipeline_state *state);
void playback_preroll(struct pipeline_state *state);
void playback_pause(struct pipeline_state *state);
void playback_seek(struct pipeline_state *state, int delta);
void playback_live(struct pipeline_state *state);
void playback_play(struct pipeline_state *state, unsigned long frame, int framerate);
void playback_play_once(struct pipeline_state *state, unsigned long start, int framerate, unsigned long count);
void playback_loop(struct pipeline_state *state, unsigned long start, int framerate, unsigned long count);
void playback_flush(struct pipeline_state *state);
void playback_cleanup(struct pipeline_state *state);

/* Video overlay control. */
void overlay_clear(struct pipeline_state *state);
void overlay_setup(struct pipeline_state *state);
void overlay_update(struct pipeline_state *state, const struct video_segment *seg);

/* RTSP live streaming */
struct rtsp_ctx *rtsp_server_launch(struct pipeline_state *state);
void rtsp_server_cleanup(struct rtsp_ctx *ctx);
void rtsp_session_foreach(struct rtsp_ctx *ctx, rtsp_session_hook_t callback, void *closure);
void rtsp_server_set_hook(struct rtsp_ctx *ctx, rtsp_session_hook_t callback, void *closure);
#define rtsp_server_clear_hook(_ctx_) rtsp_server_set_hook(_ctx_, NULL, NULL)

#endif /* __PIPELINE */
