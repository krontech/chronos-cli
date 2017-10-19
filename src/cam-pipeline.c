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

/* Signal handlering */
static sig_atomic_t catch_sigint = 0;
static void handle_sigint(int sig) { catch_sigint = 1; }
static void handle_sighup(int sig) { /* nop */ }

/* Launch a Gstreamer pipeline to run the camera live video stream */
static GstElement *
cam_pipeline(struct fpga *fpga)
{
    gboolean ret;
	GstElement *pipeline;
    GstElement *source, *queue, *scaler, *vbox, *sink;
    GstCaps *caps;
    
    /* Build the GStreamer Pipeline */
    pipeline =	gst_pipeline_new ("cam-pipeline");
	source =    gst_element_factory_make("omx_camera",      "vfcc-source");
	queue =     gst_element_factory_make("queue",           "queue");
    scaler =    gst_element_factory_make("omx_mdeiscaler",  "h264-encoder");
    vbox =      gst_element_factory_make("videobox",        "vbox");
    sink =      gst_element_factory_make("v4l2sink",        "sink");
    if (!pipeline || !source || !queue || !scaler || !vbox || !sink) {
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

	gst_bin_add_many(GST_BIN(pipeline), source, queue, scaler, vbox, sink, NULL);

    /* DEBUG: Print video frame size. */
    fprintf(stderr, "FPGA Display Configuration:\n");
    fprintf(stderr, "\tframe_address: 0x%04x\n", fpga->display->frame_address);
    fprintf(stderr, "\tfpn_address: 0x%04x\n", fpga->display->fpn_address);
    fprintf(stderr, "\tgain: 0x%04x\n", fpga->display->gain);
    fprintf(stderr, "\th_period: 0x%04x\n", fpga->display->h_period);
    fprintf(stderr, "\tv_period: 0x%04x\n", fpga->display->v_period);
    fprintf(stderr, "\th_sync_len: 0x%04x\n", fpga->display->h_sync_len);
    fprintf(stderr, "\tv_sync_len: 0x%04x\n", fpga->display->v_sync_len);
    fprintf(stderr, "\th_back_porch: 0x%04x\n", fpga->display->h_back_porch);
    fprintf(stderr, "\tv_back_porch: 0x%04x\n", fpga->display->v_back_porch);
    fprintf(stderr, "\th_res: 0x%04x\n", fpga->display->h_res);
    fprintf(stderr, "\tv_res: 0x%04x\n", fpga->display->v_res);
    fprintf(stderr, "\th_out_res: 0x%04x\n", fpga->display->h_out_res);
    fprintf(stderr, "\tv_out_res: 0x%04x\n", fpga->display->v_out_res);
    fprintf(stderr, "\tpeaking_thresh: 0x%04x\n", fpga->display->peaking_thresh);
    
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
    gst_caps_unref(caps);

    /* Video scaling should maintain the same aspect ratio, so we should check
     * if we're constrained by horizontal resolution, or vertical resolution
     */
#if 1
    /* If (LCD_HRES / CAM_VRES) > (VID_HRES / VID_VRES) */
    if ((CAM_LCD_HRES * fpga->display->v_res) > (CAM_LCD_VRES * fpga->display->h_res)) {
        /* Scaling is constrained by vertical resoluton */
        unsigned int scale_hres = (CAM_LCD_VRES * fpga->display->h_res) / fpga->display->v_res;
        fprintf(stderr, "Debug: scaling %dx%d to %d%d\n",
            fpga->display->h_res, fpga->display->v_res, scale_hres, CAM_LCD_VRES);
        caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "width", G_TYPE_INT, scale_hres,
                    "height", G_TYPE_INT, CAM_LCD_VRES,
                    NULL);
    }
    else {
        /* Scaling is constrained by horizontal resolution. */
        unsigned int scale_vres = (CAM_LCD_HRES * fpga->display->v_res) / fpga->display->h_res;
        fprintf(stderr, "Debug: scaling %dx%d to %dx%d\n",
            fpga->display->h_res, fpga->display->v_res, CAM_LCD_HRES, scale_vres);
        caps = gst_caps_new_simple ("video/x-raw-yuv",
                    "width", G_TYPE_INT, CAM_LCD_HRES,
                    "height", G_TYPE_INT, scale_vres,
                    NULL);
    }
#else
    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "width", G_TYPE_INT, CAM_LCD_HRES,
                "height", G_TYPE_INT, CAM_LCD_VRES,
                NULL);
#endif

    /* Link LCD Output capabilities. */
    ret = gst_element_link_pads_filtered(scaler, "src_00", sink, "sink", caps);
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
    do {
        /* Launch the pipeline to run live video. */
        GstElement *pipeline = cam_pipeline(fpga);
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
