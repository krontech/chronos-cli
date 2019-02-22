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
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <dirent.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>

/* For ti81xx framebuffer blending. */
#include <linux/types.h>
#include <linux/ti81xxfb.h>

#include "fpga.h"
#include "i2c.h"
#include "pipeline.h"

/* Private GStreamer elements. */
#include "gstneon.h"

#define FRAME_GRAB_PATH "/tmp/cam-frame-grab.jpg"

/* Signal handlering */
static struct pipeline_state cam_global_state = {0};
static sig_atomic_t catch_sigint = 0;

struct pipeline_state *
cam_pipeline_state(void)
{
    return &cam_global_state;
}

void
cam_pipeline_restart(struct pipeline_state *state)
{
    gst_event_ref(state->eos);
    gst_element_send_event(state->pipeline, state->eos);
}

/* Launch a Gstreamer pipeline to run the camera live video stream */
static GstElement *
cam_pipeline(struct pipeline_state *state, struct pipeline_args *args)
{
    gboolean ret;
    GstElement *tee;
    GstPad *sinkpad;
    GstPad *tpad;
    GstCaps *caps;

    /* Build the GStreamer Pipeline */
    state->pipeline = gst_pipeline_new ("pipeline");
    state->source   = gst_element_factory_make("omx_camera",  "vfcc-source");
    tee             = gst_element_factory_make("tee",         "tee");
    if (!state->pipeline || !state->source || !tee) {
        return NULL;
    }

    /* Configure elements. */
    g_object_set(G_OBJECT(state->source), "input-interface", "VIP1_PORTA", NULL);
    g_object_set(G_OBJECT(state->source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
    g_object_set(G_OBJECT(state->source), "vif-mode", "24BIT", NULL);
    g_object_set(G_OBJECT(state->source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(state->source), "skip-frames", (guint)0, NULL);

    gst_bin_add_many(GST_BIN(state->pipeline), state->source, tee, NULL);

    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('N', 'V', '1', '2'),
                "width", G_TYPE_INT, state->hres,
                "height", G_TYPE_INT, state->vres,
                "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                "buffer-count-requested", G_TYPE_INT, 4,
                NULL);
    ret = gst_element_link_filtered(state->source, tee, caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(state->pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* Create a framegrab sink and link it into the pipeline. */
    sinkpad = cam_screencap(state);
    if (!sinkpad) {
        gst_object_unref(GST_OBJECT(state->pipeline));
        return NULL;
    }
    tpad = gst_element_get_request_pad(tee, "src%d");
    gst_pad_link(tpad, sinkpad);
    gst_object_unref(sinkpad);

    /* Create the LCD sink and link it into the pipeline. */
    sinkpad = cam_lcd_sink(state, &state->config);
    if (!sinkpad) {
        gst_object_unref(GST_OBJECT(state->pipeline));
        return NULL;
    }
    tpad = gst_element_get_request_pad(tee, "src%d");
    gst_pad_link(tpad, sinkpad);
    gst_object_unref(sinkpad);

    /* Attempt to create an HDMI sink, it may fail if there is no connected display. */
    sinkpad = cam_hdmi_sink(state);
    if (sinkpad) {
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);
    }

    /* Configure for RGB Demosaiced Video. */
    state->fpga->display->pipeline &= ~(DISPLAY_PIPELINE_TEST_PATTERN | DISPLAY_PIPELINE_RAW_MODES | DISPLAY_PIPELINE_BYPASS_FPN);
    return state->pipeline;
} /* cam_pipeline */

/* Launch a GStreamer pipeline with a test souce if live display isn't active yet. */
static GstElement *
cam_videotest(struct pipeline_state *state, struct pipeline_args *args)
{
    gboolean ret;
    GstElement *queue, *ctrl, *sink;
    GstCaps *caps;

    /* Build the GStreamer Pipeline */
    state->pipeline = gst_pipeline_new ("pipeline");
    state->source   = gst_element_factory_make("videotestsrc",  "test-source");
    queue           = gst_element_factory_make("queue",         "test-queue");
    ctrl            = gst_element_factory_make("omx_ctrl",      "test-ctrl");
    sink            = gst_element_factory_make("omx_videosink", "test-sink");
    if (!state->pipeline || !state->source || !queue || !ctrl || !sink) {
        return NULL;
    }
    gst_bin_add_many(GST_BIN(state->pipeline), state->source, queue, ctrl, sink, NULL);
    gst_element_link_many(queue, ctrl, sink, NULL);

    /* Configure elements - simple video output to LCD with no scaling.  */
    g_object_set(G_OBJECT(state->source), "pattern", (guint)0, NULL);
    g_object_set(G_OBJECT(state->source), "is-live", (gboolean)1, NULL);

	g_object_set(G_OBJECT(sink), "sync", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "colorkey", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "top", (guint)state->config.yoff, NULL);
	g_object_set(G_OBJECT(sink), "left", (guint)state->config.xoff, NULL);
	g_object_set(G_OBJECT(sink), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(sink), "display-device", "LCD", NULL);

    g_object_set(G_OBJECT(ctrl), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(ctrl), "display-device", "LCD", NULL);
	
    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "width", G_TYPE_INT, state->config.hres,
                "height", G_TYPE_INT, state->config.vres,
                "framerate", GST_TYPE_FRACTION, (LIVE_MAX_FRAMERATE / 2), 1,
                "buffer-count-requested", G_TYPE_INT, 4,
                NULL);
    ret = gst_element_link_filtered(state->source, queue, caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(state->pipeline));
        return NULL;
    }
    gst_caps_unref(caps);
    return state->pipeline;
} /* cam_videotest */

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
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_PREROLL);
        state->phantom--;
    }
    return TRUE;
} /* buffer_drop_phantom */

/* Launch a gstreamer pipeline to perform video recording. */
static GstElement *
cam_filesave(struct pipeline_state *state, struct pipeline_args *args)
{
    gboolean ret;
    GstElement *tee;
    GstPad *sinkpad;
    GstPad *tpad;
    GstPad *pad;
    GstCaps *caps;

    /* Build the GStreamer Pipeline */
    state->pipeline = gst_pipeline_new ("pipeline");
    state->source   = gst_element_factory_make("omx_camera",  "vfcc-source");
    tee             = gst_element_factory_make("tee",         "tee");
    if (!state->pipeline || !state->source || !tee) {
        return NULL;
    }

    /*
     * Hack! The OMX camera element doesn't correctly flush itself when restaring
     * which leads to a phantom frame being generated in the playback stream. Drop
     * these phantom frames when starting the recording.
     * 
     * TODO: Is there a way to check for these things before starting?
     */
    state->phantom = 1;

    /* Configure elements. */
    g_object_set(G_OBJECT(state->source), "input-interface", "VIP1_PORTA", NULL);
    g_object_set(G_OBJECT(state->source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
    g_object_set(G_OBJECT(state->source), "vif-mode", "24BIT", NULL);
    g_object_set(G_OBJECT(state->source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(state->source), "skip-frames", (guint)0, NULL);
    g_object_set(G_OBJECT(state->source), "num-buffers", (guint)(args->length + state->phantom), NULL);

    gst_bin_add_many(GST_BIN(state->pipeline), state->source, tee, NULL);

    /* Add a probe to drop the very first frame from the camera */
    pad = gst_element_get_static_pad(state->source, "src");
    gst_pad_add_buffer_probe(pad, G_CALLBACK(buffer_drop_phantom), state);
    gst_object_unref(pad);

    /* Configure the input video resolution */
    state->position = args->start;
    state->loopstart = 0;
    state->loopend = state->totalframes;

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
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the H.264 sink */
        sinkpad = cam_h264_sink(state, args);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);

        /* Configure for RGB Demosaiced Video. */
        state->fpga->display->pipeline &= ~(DISPLAY_PIPELINE_TEST_PATTERN | DISPLAY_PIPELINE_RAW_MODES | DISPLAY_PIPELINE_BYPASS_FPN);
    }
    /*=====================================================
     * Setup the Pipeline for saving CinemaDNG
     *=====================================================
     */
    else if ((args->mode == PIPELINE_MODE_DNG) || (args->mode == PIPELINE_MODE_TIFF_RAW)) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-gray",
                    "bpp", G_TYPE_INT, 16,
                    "width", G_TYPE_INT, state->hres,
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        if (args->mode == PIPELINE_MODE_DNG) {
            /* Configure for Raw 16-bit padded video data. */
            sinkpad = cam_dng_sink(state, args);
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_16PAD;
        } else {
            /* Configure for Raw 12-bit padded video data. */
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_16BPP;
            sinkpad = cam_tiffraw_sink(state, args);
        }
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(state->pipeline));
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
    else if ((args->mode == PIPELINE_MODE_RAW16) || (args->mode == PIPELINE_MODE_RAW12)) {
        GstCaps *caps = gst_caps_new_simple ("video/x-raw-gray",
                    "bpp", G_TYPE_INT, 16,
                    "width", G_TYPE_INT, state->hres,
                    "height", G_TYPE_INT, state->vres,
                    "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                    "buffer-count-requested", G_TYPE_INT, 4,
                    NULL);
        ret = gst_element_link_filtered(state->source, tee, caps);
        if (!ret) {
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        sinkpad = cam_raw_sink(state, args);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);

        /* Configure for Raw 12-bit padded video data. */
        state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_16BPP;
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
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        gst_caps_unref(caps);

        /* Create the raw video sink */
        sinkpad = cam_tiff_sink(state, args);
        if (!sinkpad) {
            gst_object_unref(GST_OBJECT(state->pipeline));
            return NULL;
        }
        tpad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(tpad, sinkpad);
        gst_object_unref(sinkpad);

        /* Configure for RGB Demosaiced Video. */
        state->fpga->display->pipeline &= ~(DISPLAY_PIPELINE_TEST_PATTERN | DISPLAY_PIPELINE_RAW_MODES | DISPLAY_PIPELINE_BYPASS_FPN);
    }
    /* Otherwise, this recording mode is not supported. */
    else {
        return NULL;
    }
    return state->pipeline;
} /* cam_filesave */

static gboolean
cam_bus_watch(GstBus *bus, GstMessage *msg, gpointer data)
{
    struct pipeline_state *state = (struct pipeline_state *)data;
    GstState newstate;
    GError *error;
    gchar *debug;

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_STATE_CHANGED:
            gst_message_parse_state_changed(msg, NULL, &newstate, NULL);
            if (msg->src != GST_OBJECT_CAST(state->pipeline)) {
                break;
            }
            if (newstate == GST_STATE_PLAYING) {
                dbus_signal_sof(state);
            }
            fprintf(stderr, "Setting %s to %s...\n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name (newstate));
            break;

        case GST_MESSAGE_EOS:
            g_main_loop_quit(state->mainloop);
            break;

        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &error, &debug);
            fprintf(stderr, "GST error received from %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
            if (debug) {
                fprintf(stderr, "GST debug info: %s\n", debug);
                g_free(debug);
            }
            strncpy(state->error, error->message, sizeof(state->error));
            state->error[sizeof(state->error)-1] = '\0';
            g_error_free(error);
            g_main_loop_quit(state->mainloop);
            break;

        default:
            fprintf(stderr, "GST message received: %s\n", GST_MESSAGE_TYPE_NAME(msg));
            break;
    }

    return TRUE;
} /* cam_bus_watch */

/*===============================================
 * GLib glue to receive SIGHUP safely.
 *===============================================
 */
static gboolean
handle_sigint(gpointer data)
{
    struct pipeline_state *state = data;
    catch_sigint = 1;
    cam_pipeline_restart(state);
}

static gboolean
handle_sighup(gpointer data)
{
    struct pipeline_state *state = data;
    if (!PIPELINE_IS_SAVING(state->mode)) {
        cam_pipeline_restart(state);
    }
}

/* In modern Glib, this is a one-line call... */
#if GLIB_CHECK_VERSION(2, 30, 0)

#include <glib-unix.h>

static int
signals_init(struct pipeline_state *state)
{
    /* Shutdown and cleanup on SIGINT and SIGTERM  */    
    g_unix_signal_add(SIGTERM, handle_sigint, state);
    g_unix_signal_add(SIGINT, handle_sigint, state);
    /* Reconfigure the pipeline on SIGHUP */
    g_unix_signal_add(SIGHUP, handle_sighup, state);
    return 0;
}

#else
/*
 * The old Arago-based systems don't have signal handling helpers
 * in Glib so we need to roll our own using the self-pipe trick.
 */
static int signal_fds[2] = { -1, -1 };
static GPollFD signal_pfd;

/* The actual signal handler - just writes to the self pipe */
static void
g_unix_signal_handler(int signo)
{
    if (signo <= 255) {
        int c = signo;
        write(signal_fds[1], &c, 1);
    }
}

/* Wrappers to wake up the GMainContext on signal reception. */
static gboolean
g_unix_signal_prepare(GSource *source, gint *timeout)
{
    signal_pfd.revents = 0;
    *timeout = -1;
    return FALSE;
}

static gboolean
g_unix_signal_check(GSource *source)
{
    return (signal_pfd.revents & G_IO_IN) != 0;
}

static gboolean
g_unix_signal_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
    char signo;
    if (read(signal_fds[0], &signo, 1) != 1) {
        fprintf(stderr, "Failed to read signal from pipe: %s\n", strerror(errno));
        return TRUE;
    }
    switch (signo) {
        case SIGTERM:
        case SIGINT:
            handle_sigint(cam_pipeline_state());
            break;
        case SIGHUP:
            handle_sighup(cam_pipeline_state());
            break;
        default:
            /* Do Nothing */
            break;
    }
    return TRUE;
}

static GSourceFuncs g_unix_signal_source = {
    .prepare = g_unix_signal_prepare,
    .check = g_unix_signal_check,
    .dispatch = g_unix_signal_dispatch,
    .finalize = NULL,
};

static int
signals_init(struct pipeline_state *state)
{
    GSource *source = g_source_new(&g_unix_signal_source, sizeof(GSource));
    int flags;

    if (!source) {
        fprintf(stderr, "Failed to create Glib wakeup source\n");
        return -1;
    }

    /* Create a UNIX pipe to pass the signal safely into Glib */
    if (pipe(signal_fds) != 0) {
        fprintf(stderr, "Failed to create wakeup pipe: %s\n", strerror(errno));
        g_source_unref(source);
        return -1;
    }
    flags = fcntl(signal_fds[1], F_GETFL);
    fcntl(signal_fds[1], F_SETFL, flags | O_NONBLOCK);
    
    /* Setup to poll the read side of the pipe. */
    signal_pfd.fd = signal_fds[0];
    signal_pfd.events = G_IO_IN;
    signal_pfd.revents = 0;
    g_source_add_poll(source, &signal_pfd);
    g_source_attach(source, g_main_loop_get_context(state->mainloop));

    /* Install the POSIX signal handlers. */
    signal(SIGTERM, g_unix_signal_handler);
    signal(SIGINT,  g_unix_signal_handler);
    signal(SIGHUP,  g_unix_signal_handler);
    return 0;
}
#endif

/*===============================================
 * Pipeline Main Entry Point
 *===============================================
 */
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

/* Perform disk synchronization in a separate thread. */
static void *
fsync_worker(void *arg)
{
    struct pipeline_state *state = arg;
    struct stat st;
    while (!g_main_loop_is_running(state->mainloop)) {
        usleep(100); /* Ensure the main thread gets to g_main_loop_run. */
    }
    memset(&st, 0, sizeof(st));
    if ((fstat(state->write_fd, &st) != 0) || S_ISDIR(st.st_mode)) {
        /* TODO: Test for availability of syncfs(). */
        sync();
    } else {
        fsync(state->write_fd);
    }
    g_main_loop_quit(state->mainloop);
    return NULL;
}

int
main(int argc, char * argv[])
{
    sigset_t sigset;
    struct ti81xxfb_region_params regp;
    struct pipeline_state *state = cam_pipeline_state();
    /* Option Parsing */
    const char *short_options = "o:h";
    const struct option long_options[] = {
        {"offset",  required_argument,  0, 'o'},
        {"help",    no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };
    char *e;
    int c, fd;

    /* Set default configuration. */
    state->config.hres = CAM_LCD_HRES;
    state->config.vres = CAM_LCD_VRES;
    state->config.xoff = 0;
    state->config.yoff = 0;

    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 'o':
                parse_resolution(optarg, "OFFS", &state->config.xoff, &state->config.yoff);
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
        parse_resolution(argv[optind], "RES", &state->config.hres, &state->config.vres);
    }

    /* Initialisation */
    gst_init(&argc, &argv);
    gst_controller_init(NULL, NULL);
    if (!gst_element_register(NULL, "neon", GST_RANK_NONE, GST_TYPE_NEON)) {
        fprintf(stderr, "Failed to register Gstreamer NEON acceleration element.\n");
    }
    state->mainloop = g_main_loop_new(NULL, FALSE);
    state->eos = gst_event_new_eos();
    state->fpga = fpga_open();
    state->iops = board_chronos14_ioports;
    state->write_fd = -1;
    if (!state->fpga) {
        fprintf(stderr, "Failed to open FPGA: %s\n", strerror(errno));
        return -1;
    }

    /* Allocate a scratchpad for frame operations. */
    state->scratchpad = mmap(NULL, PIPELINE_SCRATCHPAD_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (state->scratchpad == MAP_FAILED) {
        fprintf(stderr, "Failed to allocate scratch pad: %s\n", strerror(errno));
        fpga_close(state->fpga);
        return -1;
    }

    /* Initialize the POSIX signal handlers. */
    if (signals_init(state) < 0) {
        fprintf(stderr, "Failed to setup signal handlers\n");
        fpga_close(state->fpga);
        return -1;
    }

    /* Attempt to get the camera serial number. */
    fd = open(ioport_find_by_name(state->iops, "eeprom-i2c"), O_RDWR);
    if (fd >= 0) {
        if (i2c_eeprom_read16(fd, CAMERA_SERIAL_I2CADDR, CAMERA_SERIAL_OFFSET, state->serial, sizeof(state->serial)-1) < 0) {
            memset(state->serial, 0, sizeof(state->serial));
        }
        state->serial[sizeof(state->serial)-1] = '\0';
        close(fd);
    }

    /* Attempt to configure the frame buffer pixel blending. */
    fd = open("/dev/fb0", O_RDWR);
    if (fd >= 0) {
        if (ioctl(fd, TIFB_GET_PARAMS, &regp) < 0) {
            fprintf(stderr, "Failed to read ti81xx framebuffer parameters: %s\n", strerror(errno));
        }
        else {
            regp.blendtype = TI81XXFB_BLENDING_PIXEL;
            if (ioctl(fd, TIFB_SET_PARAMS, &regp) < 0) {
                fprintf(stderr, "Failed to enable ti81xx pixel blending: %s\n", strerror(errno));
            }
        }
        close(fd);
    }

    /* Attempt to create the frame-grabber FIFO */
    if (mkfifo(SCREENCAP_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        fprintf(stderr, "Unable to create FIFO: %s\n", strerror(errno));
    }
    signal(SIGPIPE, SIG_IGN);

    /* Launch the HDMI and DBus threads. */
    hdmi_hotplug_launch(state);
    dbus_service_launch(state);
    playback_init(state);

    do {
        /* Launch the pipeline. */
        struct pipeline_args args;
        GstEvent *event;
        GstBus *bus;
        guint watchid;
        unsigned int i;

        /* Pause playback while we get setup. */
        state->next = state->args.mode;
        memset(state->error, 0, sizeof(state->error));
        memcpy(&args, &state->args, sizeof(args));
        playback_goto(state, PIPELINE_MODE_PAUSE);

        /* File saving modes should fail gracefully back to playback. */
        if (PIPELINE_IS_SAVING(args.mode)) {
            /* Return to playback mode after saving. */
            state->args.mode = PIPELINE_MODE_PLAY;
            if (!cam_filesave(state, &args)) {
                /* Throw an EOF and revert to playback. */
                dbus_signal_eof(state, state->error);
                continue;
            }
        }
        /* If the display resolution is unknown, fall back to a test pattern. */
        else if ((state->hres == 0) || (state->vres == 0)) {
            if (!cam_videotest(state, &args)) {
                dbus_signal_eof(state, state->error);
                fprintf(stderr, "Failed to launch pipeline. Aborting...\n");
                break;
            }
        }
        /* Live display and playback modes should only return fatal errors. */
        else {
            if (!cam_pipeline(state, &args)) {
                dbus_signal_eof(state, state->error);
                fprintf(stderr, "Failed to launch pipeline. Aborting...\n");
                break;
            }
        }

        /* Install a pipeline error handler. */
        bus = gst_pipeline_get_bus(GST_PIPELINE(state->pipeline));
        event = gst_event_new_flush_start();
        gst_element_send_event(state->pipeline, event);
        event = gst_event_new_flush_stop();
        gst_element_send_event(state->pipeline, event);
        watchid = gst_bus_add_watch(bus, cam_bus_watch, state);
        gst_object_unref(bus);

        playback_goto(state, state->next);
        gst_element_set_state(state->pipeline, GST_STATE_PLAYING);
        g_main_loop_run(state->mainloop);

        /* Stop the pipeline gracefully */
        playback_goto(state, PIPELINE_MODE_PAUSE);
        gst_element_set_state(state->pipeline, GST_STATE_PAUSED);
        for (i = 0; i < 1000; i++) {
            if (!g_main_context_iteration (NULL, FALSE)) break;
        }

        /* Garbage collect the pipeline. */
        state->source = NULL;
        gst_element_set_state(state->pipeline, GST_STATE_READY);
        gst_element_set_state(state->pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(state->pipeline));
        g_source_remove(watchid);

        /* Close output files that might be in progress. */
        if (state->write_fd >= 0) {
            pthread_t tid;
            if (pthread_create(&tid, NULL, fsync_worker, state)  == 0) {
                g_main_loop_run(state->mainloop);
                pthread_join(tid, NULL);
            }
            close(state->write_fd);
            state->write_fd = -1;
        }
        dbus_signal_eof(state, state->error);

        /* Add an extra newline thanks to OMX debug crap... */
        g_print("\n");
    } while(catch_sigint == 0);
    
    fprintf(stderr, "Exiting the pipeline...\n");
    unlink(SCREENCAP_PATH);
    munmap(state->scratchpad, PIPELINE_SCRATCHPAD_SIZE);
    g_main_loop_unref(state->mainloop);
    gst_event_unref(state->eos);
    fpga_close(state->fpga);
    return 0;
} /* main */
