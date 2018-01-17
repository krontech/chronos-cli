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
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <gst/gst.h>

#include "fpga.h"

#define CAM_LCD_HRES    800
#define CAM_LCD_VRES    480

/* How do we want to do scaling? */
#define CAM_SCALE_FULL      0
#define CAM_SCALE_ASPECT    1
#define CAM_SCALE_CROP      2

/* Signal handlering */
static sig_atomic_t scaler_mode = CAM_SCALE_ASPECT;
static sig_atomic_t catch_sigint = 0;
static void handle_sigint(int sig) { catch_sigint = 1; }
static void handle_sighup(int sig) { scaler_mode = (sig == SIGUSR1) ? CAM_SCALE_CROP : CAM_SCALE_ASPECT; }

/* Launch a Gstreamer pipeline to run the camera live video stream */
static GstElement *
cam_pipeline(struct fpga *fpga, int mode)
{
    gboolean ret;
	GstElement *pipeline;
    GstElement *source, *queue, *scaler, *vbox, *sink;
    GstCaps *caps;
    unsigned int scale_hres = CAM_LCD_HRES;
    unsigned int scale_vres = CAM_LCD_VRES;
    
    /* Build the GStreamer Pipeline */
    pipeline =	gst_pipeline_new ("cam-pipeline");
	source =    gst_element_factory_make("omx_camera",      "vfcc-source");
    queue =     gst_element_factory_make("queue",           "queue");
    scaler =    gst_element_factory_make("omx_mdeiscaler",  "scaler");
    sink =      gst_element_factory_make("v4l2sink",        "sink");
    if (!pipeline || !source || !queue || !scaler || !sink) {
        return NULL;
    }

    /* Configure elements. */
	g_object_set(G_OBJECT(source), "input-interface", "VIP1_PORTA", NULL);
	g_object_set(G_OBJECT(source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
	g_object_set(G_OBJECT(source), "vif-mode", "24BIT", NULL);
	g_object_set(G_OBJECT(source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(source), "skip-frames", (guint)0, NULL);

	g_object_set(G_OBJECT(sink), "sync", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "device", "/dev/video2", NULL);

	gst_bin_add_many(GST_BIN(pipeline), source, queue, scaler, sink, NULL);
    
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
    else if ((CAM_LCD_HRES * fpga->display->v_res) > (CAM_LCD_VRES * fpga->display->h_res)) {
        caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "width", G_TYPE_INT, (CAM_LCD_VRES * fpga->display->h_res) / fpga->display->v_res,
                    "height", G_TYPE_INT, CAM_LCD_VRES,
                    NULL);
    }
    else {
        caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "width", G_TYPE_INT, CAM_LCD_HRES,
                    "height", G_TYPE_INT, (CAM_LCD_HRES * fpga->display->v_res) / fpga->display->h_res,
                    NULL);
    }

    /* Link LCD Output capabilities. */
    ret = gst_element_link_pads_filtered(scaler, "src_00", sink, "sink", caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* Link the rest of the pipeline together */
    gst_element_link_many(queue, scaler, NULL);

    return pipeline;
} /* cam_pipeline */

int
main(int argc, char * argv[])
{
    struct fpga *fpga;

    /* Initialisation */
    gst_init(&argc, &argv);
    fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA: %s\n", strerror(errno));
        return -1;
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
    do {
        /* Launch the pipeline to run live video. */
        GstElement *pipeline = cam_pipeline(fpga, scaler_mode);
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

    fpga_close(fpga);
    return 0;
} /* main */