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
#include <setjmp.h>
#include <sys/types.h>
#include <gst/gst.h>
#include <jpeglib.h>

#include "fpga.h"

#define CAM_LCD_HRES    800
#define CAM_LCD_VRES    480

/* How do we want to do scaling? */
#define CAM_SCALE_FULL      0
#define CAM_SCALE_ASPECT    1
#define CAM_SCALE_CROP      2

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
};

#define FRAME_GRAB_PATH "/tmp/cam-frame-grab.jpg"

/* Signal handlering */
static sig_atomic_t scaler_mode = CAM_SCALE_ASPECT;
static sig_atomic_t catch_sighup = 0;
static sig_atomic_t catch_sigint = 0;
static void handle_sigint(int sig) { catch_sigint = 1; }

static void
handle_sighup(int sig)
{
    scaler_mode = (sig == SIGHUP) ? CAM_SCALE_ASPECT : CAM_SCALE_CROP;
}

struct jpeg_err_jmpbuf {
    struct jpeg_error_mgr pub;
    jmp_buf jmp_abort;
};

static void
jpeg_error_abort(j_common_ptr cinfo)
{
    struct jpeg_err_jmpbuf *err = (struct jpeg_err_jmpbuf *)cinfo->err;
    longjmp(err->jmp_abort, 1);
}

/* Convert NV12 semiplanar chroma data into YUV planar data. */
static inline void
jpeg_copy_chroma_nv12(JSAMPLE *u, JSAMPLE *v, JSAMPLE *chroma, unsigned int num)
{
    unsigned int i;
    for (i = 0; i < num; i++) {
        u[i] = chroma[i & ~1];
        v[i] = chroma[i | 1];
    }
}

static gboolean
buffer_framegrab(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
	struct fpga *fpga = (struct fpga *)cbdata;
    struct jpeg_compress_struct cinfo;
    struct jpeg_err_jmpbuf jerr;
    /* Planar data to the JPEG encoder. */
    JSAMPROW yrow[DCTSIZE];
    JSAMPROW urow[DCTSIZE];
    JSAMPROW vrow[DCTSIZE];
    JSAMPARRAY data[3] = { yrow, urow, vrow };
    JSAMPLE *planar;
    /* Input semiplanar data from gstreamer. */
    JSAMPLE *luma = buffer->data;
    JSAMPLE *chroma = luma + (fpga->display->h_res * fpga->display->v_res);
    unsigned int i, row;
    FILE *fp;

    /* Check if the FIFO has been opened. */
    int fd = open(FRAME_GRAB_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return TRUE;
    }
    fp = fdopen(fd, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open frame grabber output pipe: %s\n", strerror(errno));
        close(fd);
        return TRUE;
    }
    planar = malloc(sizeof(JSAMPLE) * fpga->display->h_res * DCTSIZE);
    if (!planar) {
        fprintf(stderr, "Failed to allocate working memory for JPEG encoder: %s\n", strerror(errno));
        fclose(fp);
        close(fd);
        return TRUE;
    }
    
    /* Point even and odd row pairs to the same memory to upscale the vertical chroma. */
    for (i = 0; i < DCTSIZE; i++) {
        urow[i] = planar + (fpga->display->h_res) * (i & ~1);
        vrow[i] = planar + (fpga->display->h_res) * (i | 1);
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_abort;
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    if (setjmp(jerr.jmp_abort) == 0) {
        cinfo.image_width = fpga->display->h_res;
        cinfo.image_height = fpga->display->v_res;
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_YCbCr;

        /* Prepare for raw encoding of NV12 data. */
        jpeg_set_defaults(&cinfo);
        cinfo.dct_method = JDCT_IFAST;
        cinfo.raw_data_in = TRUE;
        cinfo.comp_info[0].h_samp_factor = 1;
        cinfo.comp_info[0].v_samp_factor = 1;
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;

        jpeg_set_quality(&cinfo, 70, TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        for (row = 0; row < cinfo.image_height; row += DCTSIZE) {
            for (i = 0; i < DCTSIZE; i++) {
                if (!(i & 1)) {
                    jpeg_copy_chroma_nv12(urow[i], vrow[i], chroma, cinfo.image_width);
                    if ((row + i) < cinfo.image_height) chroma += cinfo.image_width;
                }
                yrow[i] = luma;
                if ((row + i) < cinfo.image_height) luma += cinfo.image_width;
            }
            jpeg_write_raw_data(&cinfo, data, DCTSIZE);
        }
        jpeg_finish_compress(&cinfo);
    }
    jpeg_destroy_compress(&cinfo);
    free(planar);
    fclose(fp);
    close(fd);
    return TRUE;
}

/* Launch a Gstreamer pipeline to run the camera live video stream */
static GstElement *
cam_pipeline(struct fpga *fpga, struct display_config *config, int mode)
{
    gboolean ret;
	GstElement *pipeline;
    GstElement *source, *queue, *scaler, *ctrl, *sink;
    GstCaps *caps;
	GstPad *pad;
    unsigned int scale_mul = 1, scale_div = 1;
    unsigned int hout, vout;
    unsigned int hoff, voff;
    
    /* Build the GStreamer Pipeline */
    pipeline =	gst_pipeline_new ("cam-pipeline");
	source =    gst_element_factory_make("omx_camera",      "vfcc-source");
    queue =     gst_element_factory_make("queue",           "queue");
    scaler =    gst_element_factory_make("omx_mdeiscaler",  "scaler");
    ctrl =      gst_element_factory_make("omx_ctrl",        "ctrl");
    sink =      gst_element_factory_make("omx_videosink",   "sink");
    if (!pipeline || !source || !queue || !scaler || !ctrl || !sink) {
        return NULL;
    }

    /* Configure elements. */
	g_object_set(G_OBJECT(source), "input-interface", "VIP1_PORTA", NULL);
	g_object_set(G_OBJECT(source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
	g_object_set(G_OBJECT(source), "vif-mode", "24BIT", NULL);
	g_object_set(G_OBJECT(source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(source), "skip-frames", (guint)0, NULL);

	g_object_set(G_OBJECT(sink), "sync", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(sink), "display-device", "LCD", NULL);
	g_object_set(G_OBJECT(ctrl), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(ctrl), "display-device", "LCD", NULL);

	gst_bin_add_many(GST_BIN(pipeline), source, queue, scaler, ctrl, sink, NULL);
    
    /* Grab frames from the queue */
	pad = gst_element_get_static_pad(queue, "src");
	gst_pad_add_buffer_probe(pad, G_CALLBACK(buffer_framegrab), fpga);
	gst_object_unref(pad);

    /* Link OMX Source input capabilities. */
	caps = gst_caps_new_simple ("video/x-raw-yuv",
                "format", GST_TYPE_FOURCC,
                GST_MAKE_FOURCC('N', 'V', '1', '2'),
                "width", G_TYPE_INT, fpga->display->h_res,
                "height", G_TYPE_INT, fpga->display->v_res,
                "framerate", GST_TYPE_FRACTION, 60, 1,
                "buffer-count-requested", G_TYPE_INT, 4,
                NULL);
    ret = gst_element_link_filtered(source, queue, caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* No scaling, just let the pipeline crop as necessary. */
#if 0
    if (mode == CAM_SCALE_CROP) {
        caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "width", G_TYPE_INT, fpga->display->h_res,
                    "height", G_TYPE_INT, fpga->display->v_res,
                    NULL);
    }
    else if (mode == CAM_SCALE_FULL) {
        /* Scale image to full screen, possibly stretching it out of proprortion. */
        caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "width", G_TYPE_INT, CAM_LCD_HRES,
                    "height", G_TYPE_INT, CAM_LCD_VRES,
                    NULL);
    }
    /* Otherwise, scale the image while retaining the same aspect ratio. */
    else
#endif
    if ((config->hres * fpga->display->v_res) > (config->vres * fpga->display->h_res)) {
        scale_mul = config->vres;
        scale_div = fpga->display->v_res;
    }
    else {
        scale_mul = config->hres;
        scale_div = fpga->display->h_res;
    }
    hout = ((fpga->display->h_res * scale_mul) / scale_div) & ~0xF;
    vout = ((fpga->display->v_res * scale_mul) / scale_div) & ~0x1;
    hoff = (config->xoff + (config->hres - hout) / 2) & ~0x1;
    voff = (config->yoff + (config->vres - vout) / 2) & ~0x1;

#ifdef DEBUG
    fprintf(stderr, "DEBUG: scale = %u/%u\n", scale_mul, scale_div);
    fprintf(stderr, "DEBUG: input = [%u, %u]\n", fpga->display->h_res, fpga->display->v_res);
    fprintf(stderr, "DEBUG: output = [%u, %u]\n", hout, vout);
    fprintf(stderr, "DEBUG: offset = [%u, %u]\n", hoff, voff);
#endif
	g_object_set(G_OBJECT(sink), "top", (guint)voff, NULL);
	g_object_set(G_OBJECT(sink), "left", (guint)hoff, NULL);
    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "width", G_TYPE_INT, hout,
                "height", G_TYPE_INT, vout,
                NULL);

    /* Link LCD Output capabilities. */
    ret = gst_element_link_pads_filtered(scaler, "src_00", ctrl, "sink", caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* Link the rest of the pipeline together */
    gst_element_link_many(queue, scaler, NULL);
    gst_element_link_many(ctrl, sink, NULL);

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
    struct fpga *fpga;
    /* Default to use the entire LCD screen. */
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
    fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA: %s\n", strerror(errno));
        return -1;
    }
    
    /* Attempt to create the frame-grabber FIFO */
    if (mkfifo(FRAME_GRAB_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        fprintf(stderr, "Unable to create FIFO: %s\n", strerror(errno));
    }

    /*
     * Run the pipeline in a loop, reconfiguring on signal reception.
     * OMX Segfaults on restart, so the only way to make this work
     * is to garbage collect the pipeline and start again from scratch.
     */
    signal(SIGTERM, handle_sigint);
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sighup);
    signal(SIGUSR1, handle_sighup);
    signal(SIGUSR2, handle_sighup);
    signal(SIGPIPE, SIG_IGN);
    do {
        /* Launch the pipeline to run live video. */
        GstElement *pipeline = cam_pipeline(fpga, &config, scaler_mode);
        GstEvent *event;
        if (!pipeline) {
            fprintf(stderr, "Failed to launch pipeline. Aborting...\n");
            break;
        }

        g_print ("Setting pipeline to PLAYING...\n");
        gst_element_set_state (pipeline, GST_STATE_PLAYING);

        /* Run the pipeline until we get a signal. */
        pause();

        /* Stop the pipeline gracefully */
        g_print("Setting pipeline to PAUSED...\n");
        event = gst_event_new_eos();
        gst_element_send_event(pipeline, event);
        gst_element_set_state (pipeline, GST_STATE_PAUSED);
        while (g_main_context_iteration (NULL, FALSE));

        /* Garbage collect the pipeline. */
        gst_element_set_state (pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
        /* Add an extra newline thanks to OMX debug crap... */
        g_print("\n");
    } while(catch_sigint == 0);

    unlink(FRAME_GRAB_PATH);
    fpga_close(fpga);
    return 0;
} /* main */
