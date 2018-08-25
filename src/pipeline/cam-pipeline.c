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
#include <sys/signalfd.h>
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
static struct pipeline_state cam_global_state = {0};
static sig_atomic_t scaler_mode = CAM_SCALE_ASPECT;
static sig_atomic_t catch_sigint = 0;

struct pipeline_state *
cam_pipeline_state(void)
{
    return &cam_global_state;
}

static void handle_sigint(int sig)
{
    catch_sigint = 1;
    g_main_loop_quit(cam_pipeline_state()->mainloop);
}

static void
handle_sighup(int sig)
{
    struct pipeline_state *state = cam_pipeline_state();
    if (!PIPELINE_IS_RECORDING(state->mode)) {
        scaler_mode = (sig == SIGHUP) ? CAM_SCALE_ASPECT : CAM_SCALE_CROP;
        g_main_loop_quit(state->mainloop);
    }
}

/* Launch a Gstreamer pipeline to run the camera live video stream */
static GstElement *
cam_pipeline(struct pipeline_state *state, struct display_config *config, struct pipeline_args *args)
{
    gboolean ret;
    GstElement *pipeline;
    GstElement *tee;
    GstPad *sinkpad;
    GstPad *tpad;
    GstCaps *caps;
    
    /* Build the GStreamer Pipeline */
    pipeline        = gst_pipeline_new ("pipeline");
    state->source   = gst_element_factory_make("omx_camera",  "vfcc-source");
    tee             = gst_element_factory_make("tee",         "tee");
    if (!pipeline || !state->source || !tee) {
        return NULL;
    }
    /* Configure elements. */
    g_object_set(G_OBJECT(state->source), "input-interface", "VIP1_PORTA", NULL);
    g_object_set(G_OBJECT(state->source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
    g_object_set(G_OBJECT(state->source), "vif-mode", "24BIT", NULL);
    g_object_set(G_OBJECT(state->source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(state->source), "skip-frames", (guint)0, NULL);

    gst_bin_add_many(GST_BIN(pipeline), state->source, tee, NULL);

    /* Configure the input video resolution */
    state->hres = state->fpga->display->h_res;
    state->vres = state->fpga->display->v_res;
    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "format", GST_TYPE_FOURCC,
                GST_MAKE_FOURCC('N', 'V', '1', '2'),
                "width", G_TYPE_INT, state->hres,
                "height", G_TYPE_INT, state->vres,
                "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                "buffer-count-requested", G_TYPE_INT, 4,
                NULL);
    ret = gst_element_link_filtered(state->source, tee, caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* Create a framegrab sink and link it into the pipeline. */
    sinkpad = cam_screencap(state, pipeline);
    if (!sinkpad) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    tpad = gst_element_get_request_pad(tee, "src%d");
    gst_pad_link(tpad, sinkpad);
    gst_object_unref(sinkpad);

    /* Create the LCD sink and link it into the pipeline. */
    sinkpad = cam_lcd_sink(state, pipeline, config);
    if (!sinkpad) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    tpad = gst_element_get_request_pad(tee, "src%d");
    gst_pad_link(tpad, sinkpad);
    gst_object_unref(sinkpad);

    /* Attempt to create an HDMI sink, it may fail if there is no connected display. */
    sinkpad = cam_hdmi_sink(state, pipeline);
    if (sinkpad) {
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }
    return pipeline;
} /* cam_pipeline */

/*
 * Workaround for OMX buffering bug in the omx_camera element, which doesn't
 * flush itself when restarting. This leads to a garbled first frame in the
 * input stream from the last camera activity.
 */
static gboolean
buffer_drop_phantom(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    if (state->phantom) {
        state->phantom--;
        return FALSE;
    }
    return TRUE;
} /* buffer_drop_phantom */

/* Launch a gstreamer pipeline to perform video recording. */
static GstElement *
cam_recorder(struct pipeline_state *state, struct display_config *config, struct pipeline_args *args)
{
    gboolean ret;
    GstElement *pipeline;
    GstElement *tee;
    GstPad *sinkpad;
    GstPad *tpad;
    GstPad *pad;
    GstCaps *caps;

    /* Build the GStreamer Pipeline */
    pipeline        = gst_pipeline_new ("pipeline");
    state->source   = gst_element_factory_make("omx_camera",  "vfcc-source");
    tee             = gst_element_factory_make("tee",         "tee");
    if (!pipeline || !state->source || !tee) {
        return NULL;
    }

    /*
     * Hack! The OMX camera element doesn't correctly flush itself when restaring
     * which leads to a phantom frame being generated in the playback stream. Drop
     * these phantom frames when starting the recording.
     * 
     * TODO: Is there a way to check for these things before starting?
     * BUG: The mp4mux element appears corrupt the mp4 metadata when the first frame
     *      is dropped, so we only do the phantom dropping for raw modes.
     */
    if (args->mode != PIPELINE_MODE_H264) {
        state->phantom = 1;
    } else {
        state->phantom = 0;
    }

    /* Configure elements. */
    g_object_set(G_OBJECT(state->source), "input-interface", "VIP1_PORTA", NULL);
    g_object_set(G_OBJECT(state->source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
    g_object_set(G_OBJECT(state->source), "vif-mode", "24BIT", NULL);
    g_object_set(G_OBJECT(state->source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(state->source), "skip-frames", (guint)0, NULL);
    g_object_set(G_OBJECT(state->source), "num-buffers", (guint)(args->length + state->phantom), NULL);

    gst_bin_add_many(GST_BIN(pipeline), state->source, tee, NULL);

    /* Add a probe to drop the very first frame from the camera */
    pad = gst_element_get_static_pad(state->source, "src");
    gst_pad_add_buffer_probe(pad, G_CALLBACK(buffer_drop_phantom), state);
    gst_object_unref(pad);

    /* Configure the input video resolution */
    state->hres = state->fpga->display->h_res;
    state->vres = state->fpga->display->v_res;
    state->position = args->start;

    /*=====================================================
     * Setup the Pipeline in H.264 Recording Mode
     *=====================================================
     */
    if (args->mode == PIPELINE_MODE_H264) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "format", GST_TYPE_FOURCC,
                    GST_MAKE_FOURCC('N', 'V', '1', '2'),
                    "width", G_TYPE_INT, state->hres,
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, args->framerate, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the H.264 sink */
        sinkpad = cam_h264_sink(state, args, pipeline);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }
    /*=====================================================
     * Setup the Pipeline for saving CinemaDNG
     *=====================================================
     */
    else if (args->mode == PIPELINE_MODE_DNG) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-gray",
                    "bpp", G_TYPE_INT, 16,
                    "width", G_TYPE_INT, state->hres,
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        sinkpad = cam_dng_sink(state, args, pipeline);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }
    /*=====================================================
     * Setup the Pipeline in 12/16-bit Raw Recording Mode
     *=====================================================
     */
    else if (args->mode == PIPELINE_MODE_RAW16) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-gray",
                    "bpp", G_TYPE_INT, 16,
                    "width", G_TYPE_INT, state->hres,
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        sinkpad = cam_raw_sink(state, args, pipeline);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }
    /*=====================================================
     * Setup the Pipeline in 12-bit Packed Recording Mode
     *=====================================================
     */
    else if (args->mode == PIPELINE_MODE_RAW12) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-rgb",
                    "bpp", G_TYPE_INT, 24,
                    "width", G_TYPE_INT, state->hres / 2, /* 2x pixels packed per clock */
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        sinkpad = cam_raw_sink(state, args, pipeline);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }
    /*=====================================================
     * Setup the Pipeline for saving raw RGB pixel data.
     *=====================================================
     */
    else if (args->mode == PIPELINE_MODE_TIFF) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-rgb",
                    "bpp", G_TYPE_INT, 24,
                    "width", G_TYPE_INT, state->hres,
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        sinkpad = cam_tiff_sink(state, args, pipeline);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }
    /* Otherwise, this recording mode is not supported. */
    else {
        return NULL;
    }
    return pipeline;
} /* cam_recorder */

static gboolean
cam_bus_watch(GstBus *bus, GstMessage *msg, gpointer data)
{
    struct pipeline_state *state = (struct pipeline_state *)data;

    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STATE_CHANGED) {
        GstState newstate;
        gst_message_parse_state_changed(msg, NULL, &newstate, NULL);
        if (!strcmp(GST_OBJECT_NAME(msg->src), "pipeline")) {
            fprintf(stderr, "Setting %s to %s...\n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name (newstate));
        }
    }
    else {
        fprintf(stderr, "GST message received: %s\n", GST_MESSAGE_TYPE_NAME(msg));
    }

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
            g_main_loop_quit(state->mainloop);
            break;

        case GST_MESSAGE_ERROR:
            g_main_loop_quit(state->mainloop);
            break;

#if 0
            gchar  *debug;
            GError *error;

            gst_message_parse_error (msg, &error, &debug);

            g_printerr ("Error: %s\n", error->message);
            gstDia->error = true;
            if(gstDia->errorCallback)
                (*gstDia->errorCallback)(gstDia->errorCallbackArg, error->message);
            g_error_free (error);
            break;
#endif

        case GST_MESSAGE_ASYNC_DONE:
            dbus_signal_sof(state);
            break;
        default:
            break;
    }
} /* cam_bus_watch */

static void
usage(FILE *fp, int argc, char *argv[])
{
    fprintf(fp, "Usage: %s [options] [RES]\n\n", argv[0]);

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
    sigset_t sigset;
    struct pipeline_state *state = cam_pipeline_state();
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
    gst_init(&argc, &argv);
    state->mainloop = g_main_loop_new(NULL, FALSE);
    state->fpga = fpga_open();
    state->iops = board_chronos14_ioports;
    state->write_fd = -1;
    if (!state->fpga) {
        fprintf(stderr, "Failed to open FPGA: %s\n", strerror(errno));
        return -1;
    }
    
    /* Attempt to create the frame-grabber FIFO */
    if (mkfifo(SCREENCAP_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        fprintf(stderr, "Unable to create FIFO: %s\n", strerror(errno));
    }

    /* Launch the HDMI and DBus threads. */
    hdmi_hotplug_launch(state);
    dbus_service_launch(state);
    playback_init(state);
    
    /* Shutdown and cleanup on SIGINT and SIGTERM  */
    signal(SIGTERM, handle_sigint);
    signal(SIGINT, handle_sigint);

    /* Reconfigure the pipeline on SIGHUP and SIGUSR */
    signal(SIGHUP, handle_sighup);
    signal(SIGUSR1, handle_sighup);
    signal(SIGUSR2, handle_sighup);
    signal(SIGPIPE, SIG_IGN);
    do {
        /* Launch the pipeline. */
        struct pipeline_args args;
        GstElement *pipeline;
        GstEvent *event;
        GstBus *bus;
        guint watchid;
        unsigned int i;

        /* Pause playback while we get setup. */
        memcpy(&args, &state->args, sizeof(args));
        playback_goto(state, PIPELINE_MODE_PAUSE);

        /* Recording modes should fail gracefully back to playback. */
        if (PIPELINE_IS_RECORDING(args.mode)) {
            /* Return to playback mode after recording. */
            state->args.mode = PIPELINE_MODE_PLAY;

            pipeline = cam_recorder(state, &config, &args);
            if (!pipeline) {
                /* Throw an EOF and revert to playback. */
                dbus_signal_eof(state);
                state->args.mode = PIPELINE_MODE_PLAY;
                continue;
            }
        }
        /* Live display and playback modes should only return fatal errors. */
        else {
            memset(&state->args, 0, sizeof(state->args));
            pipeline = cam_pipeline(state, &config, &args);
            if (!pipeline) {
                fprintf(stderr, "Failed to launch pipeline. Aborting...\n");
                break;
            }
        }

        /* Install an pipeline error handler. */
        bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
        event = gst_event_new_flush_start();
        gst_element_send_event(pipeline, event);
        event = gst_event_new_flush_stop();
        gst_element_send_event(pipeline, event);
        watchid = gst_bus_add_watch(bus, cam_bus_watch, state);
        gst_object_unref(bus);

        playback_goto(state, args.mode);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        g_main_loop_run(state->mainloop);

        /* Stop the pipeline gracefully */
        dbus_signal_eof(state);
        event = gst_event_new_eos();
        gst_element_send_event(pipeline, event);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        for (i = 0; i < 1000; i++) {
            if (!g_main_context_iteration (NULL, FALSE)) break;
        }

        /* Garbage collect the pipeline. */
        state->source = NULL;
        gst_element_set_state(pipeline, GST_STATE_READY);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
        g_source_remove(watchid);

        /* Close any output files that might be in progress. */
        if (state->write_fd >= 0) {
            fsync(state->write_fd);
            close(state->write_fd);
            sync();
            state->write_fd = -1;
        }

        /* Add an extra newline thanks to OMX debug crap... */
        g_print("\n");
    } while(catch_sigint == 0);

    unlink(SCREENCAP_PATH);
    fpga_close(state->fpga);
    return 0;
} /* main */
