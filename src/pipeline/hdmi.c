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
#include <pthread.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <gst/gst.h>

/* For HDMI detection. */
#include <linux/types.h>
#include <linux/ti81xxhdmi.h>

#include "edid.h"
#include "pipeline.h"

static void *
hdmi_hotplug_thread(void *arg)
{
    int prev_status = -1;
    int fd = open("/dev/TI81XX_HDMI", O_RDWR);
    while (fd >= 0) {
        struct ti81xxhdmi_hpd_status status;
        int err = ioctl(fd, TI81XXHDMI_WAIT_FOR_HPD_CHANGE, &status);
        if (err < 0) {
            fprintf(stderr, "DEBUG: ioctl(TI81XXHDMI_WAIT_FOR_HPD_CHANGE) error: %s\n", strerror(errno));
            continue;
        }
        if (status.hpd_status != prev_status) {
            fprintf(stderr, "HDMI Hotplug event detected\n");
            pthread_kill((uintptr_t)arg, SIGHUP);
        }
        prev_status = status.hpd_status;
    }
    return NULL;
}

void
hdmi_hotplug_launch(struct pipeline_state *state)
{
    pthread_t main_thread = pthread_self();
    pthread_t hdmi_thread;
    pthread_create(&hdmi_thread, NULL, hdmi_hotplug_thread, (void *)(uintptr_t)pthread_self());
}

static int
set_hdmi_mode(const char *modestr)
{
    FILE *ena;
    FILE *mode = fopen("/sys/devices/platform/vpss/display0/mode", "w");
    char readmode[64];
    
    /* If the mode already matches, then do nothing. */
    if (strcmp(readmode, modestr) == 0) {
        fclose(mode);
        return 0;
    }

    /* Disable HDMI, change the mode and enable it again. */
    ena = fopen("/sys/devices/platform/vpss/display0/enabled", "w");
    if (ena) {
        fputs("0", ena);
        fclose(ena);
    }
    fputs(modestr, mode);
    fclose(mode);
    ena = fopen("/sys/devices/platform/vpss/display0/enabled", "w");
    if (ena) {
        fputs("1", ena);
        fclose(ena);
    }
}

/*
 * Format is:
 *      pxclock,hres/hfrontporch/hbackporch/hsync,vres/vfrontporch/vbackporch/vsync,progressive
 */

GstPad *
cam_hdmi_sink(GstElement *pipeline, unsigned long input_hres, unsigned long input_vres)
{
    int pref, err;
    int fd = open("/dev/TI81XX_HDMI", O_RDWR);
    union {
        struct edid_data data;
        uint64_t pad[512 / sizeof(uint64_t)];
    } u;
    const char *omx_mode = "OMX_DC_MODE_1080P_60";
    unsigned int panel_refresh, panel_hres, panel_vres;
    unsigned int hout, vout;
    unsigned int hoff, voff;
    unsigned int scale_mul = 1, scale_div = 1;
    gboolean ret;
    GstElement *queue, *scaler, *ctrl, *sink;
    GstCaps *caps;

    if (fd < 0) {
        /* No HDMI module. */
        return NULL;
    }

    /* Read the EDID data via HDMI */
    err = ioctl(fd, TI81XXHDMI_READ_EDID, &u.data);
    if (err < 0) {
        /* No HDMI display is connected. */
        close(fd);
        return NULL;
    }
    close(fd);
    if (!edid_sanity(&u.data)) {
        /* Invalid EDID data */
        return NULL;
    }

    pref = 0;
    while (1) {
        panel_refresh = edid_get_timing(&u.data, pref, &panel_hres, &panel_vres);
        if (!panel_refresh) {
            /* No supported display timing. */
            return NULL;
        }
        /* OMX only supports 1080p and 720p for now. */
        if ((panel_hres == 1920) && (panel_vres == 1080) && (panel_refresh == 60)) {
            set_hdmi_mode("1080p-60");
            omx_mode = "OMX_DC_MODE_1080P_60";
            break;
        }
        if ((panel_hres == 1280) && (panel_vres == 720) && (panel_refresh == 30)) {
            set_hdmi_mode("720p-60");
            omx_mode = "OMX_DC_MODE_720P_60";
            break;
        }
        pref++;
    }

    /* Pipeline setup */
    queue =     gst_element_factory_make("queue",           "hdmiqueue");
    scaler =    gst_element_factory_make("omx_mdeiscaler",  "hdmiscaler");
    ctrl =      gst_element_factory_make("omx_ctrl",        "hdmictrl");
    sink =      gst_element_factory_make("omx_videosink",   "hdmisink");
    if (!queue || !scaler || !ctrl || !sink) {
        return NULL;
    }

	g_object_set(G_OBJECT(sink), "sync", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "display-mode", omx_mode, NULL);
	g_object_set(G_OBJECT(sink), "display-device", "HDMI", NULL);
	g_object_set(G_OBJECT(ctrl), "display-mode", omx_mode, NULL);
	g_object_set(G_OBJECT(ctrl), "display-device", "HDMI", NULL);

	gst_bin_add_many(GST_BIN(pipeline), queue, scaler, ctrl, sink, NULL);

    if ((panel_hres * input_vres) > (panel_vres * input_hres)) {
        scale_mul = panel_vres;
        scale_div = input_vres;
    }
    else {
        scale_mul = panel_hres;
        scale_div = input_hres;
    }
    hout = ((input_hres * scale_mul) / scale_div) & ~0xF;
    vout = ((input_vres * scale_mul) / scale_div) & ~0x1;
    hoff = ((panel_hres - hout) / 2) & ~0x1;
    voff = ((panel_vres - vout) / 2) & ~0x1;

#ifdef DEBUG
    fprintf(stderr, "DEBUG: scale = %u/%u\n", scale_mul, scale_div);
    fprintf(stderr, "DEBUG: input = [%lu, %lu]\n", input_hres, input_vres);
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

    /* Return the first element of our segment to link with */
    gst_element_link_many(queue, scaler, NULL);
    gst_element_link_many(ctrl, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
}
