/****************************************************************************
 *  Copyright (C) 2017 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <gst/gst.h>

#include "fpga.h"
#include "pipeline.h"


#define CAM_LCD_HRES    800
#define CAM_LCD_VRES    480

/* How do we want to do scaling? */
#define CAM_SCALE_FULL      0
#define CAM_SCALE_ASPECT    1
#define CAM_SCALE_CROP      2

#define FRAME_GRAB_PATH "/tmp/cam-frame-grab.jpg"

/* Signal handlering */
static struct pipeline_state *pstate;
static sig_atomic_t scaler_mode = CAM_SCALE_ASPECT;
static sig_atomic_t catch_sigint = 0;

static void handle_sigint(int sig)
{
    catch_sigint = 1;
    g_main_loop_quit(pstate->mainloop);
}

static void
handle_sighup(int sig)
{
    scaler_mode = (sig == SIGHUP) ? CAM_SCALE_ASPECT : CAM_SCALE_CROP;
    g_main_loop_quit(pstate->mainloop);
}

/* Display a specific frame when in playback mode. */
static void
display_frameno(struct pipeline_state *state, unsigned long frame)
{
    unsigned long f_size = state->fpga->seq->frame_size;
    unsigned long count = (state->region_size / f_size);
    unsigned long offset = (state->region_first - state->region_base) / f_size;
    if (count) {
        state->fpga->display->frame_address = state->region_base + (((frame + offset) % count) * f_size);
    } else {
        state->fpga->display->frame_address = state->region_base;
    }
    state->fpga->display->manual_sync = 1;
}

static void
handle_sigalrm(int sig)
{
    int divisor = (pstate->playrate + LIVE_MAX_FRAMERATE - 1) / LIVE_MAX_FRAMERATE;
    if (pstate->totalframes && pstate->playrate) {
        long nextframe = (pstate->lastframe + divisor) % pstate->totalframes;
        if (nextframe < 0) pstate->totalframes += pstate->totalframes;
        pstate->lastframe = nextframe;
    }
    /* Display the desired frame. */
    if (pstate->fpga->display->control & DISPLAY_CTL_ADDRESS_SELECT) {
        unsigned long f_size = pstate->fpga->seq->frame_size;
        display_frameno(pstate, pstate->lastframe);
    }
}

/* Launch a Gstreamer pipeline to run the camera live video stream */
static GstElement *
cam_pipeline(struct fpga *fpga, struct display_config *config, int mode)
{
    gboolean ret;
	GstElement *pipeline;
    GstElement *source, *tee;
    GstPad *sinkpad;
    GstCaps *caps;
	GstPad *tpad;
    
    /* Build the GStreamer Pipeline */
    pipeline =	gst_pipeline_new ("cam-pipeline");
	source =    gst_element_factory_make("omx_camera",  "vfcc-source");
    tee =       gst_element_factory_make("tee",         "tee");
    if (!pipeline || !source || !tee) {
        return NULL;
    }
    /* Configure elements. */
	g_object_set(G_OBJECT(source), "input-interface", "VIP1_PORTA", NULL);
	g_object_set(G_OBJECT(source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
	g_object_set(G_OBJECT(source), "vif-mode", "24BIT", NULL);
	g_object_set(G_OBJECT(source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(source), "skip-frames", (guint)0, NULL);

	gst_bin_add_many(GST_BIN(pipeline), source, tee, NULL);

    /* Link OMX Source input capabilities. */
	caps = gst_caps_new_simple ("video/x-raw-yuv",
                "format", GST_TYPE_FOURCC,
                GST_MAKE_FOURCC('N', 'V', '1', '2'),
                "width", G_TYPE_INT, fpga->display->h_res,
                "height", G_TYPE_INT, fpga->display->v_res,
                "framerate", GST_TYPE_FRACTION, 60, 1,
                "buffer-count-requested", G_TYPE_INT, 4,
                NULL);
    ret = gst_element_link_filtered(source, tee, caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* Create a framegrab sink and link it into the pipeline. */
    sinkpad = cam_screencap(pipeline);
    if (!sinkpad) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    tpad = gst_element_get_request_pad(tee, "src%d");
    gst_pad_link(tpad, sinkpad);
    gst_object_unref(sinkpad);

    /* Create the LCD sink and link it into the pipeline. */
    sinkpad = cam_lcd_sink(pipeline, fpga->display->h_res, fpga->display->v_res, config);
    if (!sinkpad) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    tpad = gst_element_get_request_pad(tee, "src%d");
    gst_pad_link(tpad, sinkpad);
    gst_object_unref(sinkpad);

    /* Attempt to create an HDMI sink, it may fail if there is no connected display. */
    sinkpad = cam_hdmi_sink(pipeline, fpga->display->h_res, fpga->display->v_res);
    if (sinkpad) {
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }

    return pipeline;
} /* cam_pipeline */

static void
usage(FILE *fp, int argc, char *argv[])
{
    fprintf(fp, "usage : %s [options] [RES]\n\n", argv[0]);

    fprintf(fp, "Operate the video pipeline on the Chronos camera.\n\n");
    fprintf(fp, "The output resolution may be provided as a string with the\n");
    fprintf(fp, "horizontal and vertical resolutions separated with an 'x' (ie:\n");
    fprintf(fp, "640x480). Otherwise, the default resolution is %ux%u\n\n", CAM_LCD_HRES, CAM_LCD_VRES);

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-o, --offset OFFS  offset the output by OFFS pixels\n");
    fprintf(fp, "\t-h, --help         display this help and exit\n");
} /* usage */

static void
parse_resolution(const char *str, const char *name, unsigned long *x, unsigned long *y)
{
    while (1) {
        char *end;
        *x = strtoul(str, &end, 10);
        if (*end != 'x') break;
        *y = strtoul(end+1, &end, 10);
        if (*end != '\0') break;
        return;
    }
    fprintf(stderr, "Failed to parse %s from \'%s\'\n", name, str);
    exit(EXIT_FAILURE);
} /* parse_resolution */

int
main(int argc, char * argv[])
{
    struct itimerspec ts;
    struct pipeline_state state = {0};
    struct sigevent sigev;
    struct sigaction sigact;
    struct display_config config = {
        .hres = CAM_LCD_HRES,
        .vres = CAM_LCD_VRES,
        .xoff = 0,
        .yoff = 0,
    };
    /* Option Parsing */
    const char *short_options = "o:h";
    const struct option long_options[] = {
        {"offset",  required_argument,  0, 'o'},
        {"help",    no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };
    char *e;
    int c;
    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 'o':
                parse_resolution(optarg, "OFFS", &config.xoff, &config.yoff);
                break;

            case 'h':
                usage(stdout, argc, argv);
                return EXIT_SUCCESS;
            case '?':
            default:
                return EXIT_FAILURE;
        }
    }
    /* If there is another argument, parse it as the display resolution. */
    if (optind < argc) {
        parse_resolution(argv[optind], "RES", &config.hres, &config.vres);
    }

    /* Initialisation */
    pstate = &state;
    gst_init(&argc, &argv);
    state.mainloop = g_main_loop_new(NULL, FALSE);
    state.fpga = fpga_open();
    if (!state.fpga) {
        fprintf(stderr, "Failed to open FPGA: %s\n", strerror(errno));
        return -1;
    }
    
    /* Attempt to create the frame-grabber FIFO */
    if (mkfifo(SCREENCAP_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        fprintf(stderr, "Unable to create FIFO: %s\n", strerror(errno));
    }

    /* Launch the HDMI and DBus threads. */
    hdmi_hotplug_launch(&state);
    dbus_service_launch(&state);
    
    /* Create the timer used for driving the playback state machine. */
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_value.sival_ptr = &state;
    timer_create(CLOCK_MONOTONIC, &sigev, &state.playtimer);

    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 1000000000UL / LIVE_MAX_FRAMERATE;
    ts.it_value.tv_sec = 0;
    ts.it_value.tv_nsec = ts.it_interval.tv_nsec;
    timer_settime(state.playtimer, 0, &ts, NULL);

    /*
     * Run the pipeline in a loop, reconfiguring on signal reception.
     * OMX Segfaults on restart, so the only way to make this work
     * is to garbage collect the pipeline and start again from scratch.
     */
    signal(SIGTERM, handle_sigint);
    signal(SIGINT, handle_sigint);
    signal(SIGALRM, handle_sigalrm);
    signal(SIGHUP, handle_sighup);
    signal(SIGUSR1, handle_sighup);
    signal(SIGUSR2, handle_sighup);
    signal(SIGPIPE, SIG_IGN);
    do {
        /* Launch the pipeline to run live video. */
        GstElement *pipeline = cam_pipeline(state.fpga, &config, scaler_mode);
        GstEvent *event;
        unsigned int i;
        if (!pipeline) {
            fprintf(stderr, "Failed to launch pipeline. Aborting...\n");
            break;
        }

        g_print ("Setting pipeline to PLAYING...\n");
        gst_element_set_state (pipeline, GST_STATE_PLAYING);

        /* Run the pipeline until we get a signal. */
        g_main_loop_run(state.mainloop);

        /* Stop the pipeline gracefully */
        g_print("Setting pipeline to PAUSED...\n");
        event = gst_event_new_eos();
        gst_element_send_event(pipeline, event);
        gst_element_set_state (pipeline, GST_STATE_PAUSED);
        for (i = 0; i < 1000; i++) {
            if (!g_main_context_iteration (NULL, FALSE)) break;
        }

        /* Garbage collect the pipeline. */
        gst_element_set_state (pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
        /* Add an extra newline thanks to OMX debug crap... */
        g_print("\n");
    } while(catch_sigint == 0);

    unlink(SCREENCAP_PATH);
    fpga_close(state.fpga);
    return 0;
} /* main */
