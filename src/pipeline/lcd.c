
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <gst/gst.h>

#include "pipeline.h"

GstPad *
cam_lcd_sink(struct pipeline_state *state, const struct display_config *output)
{
    gboolean ret;
    GstElement *queue, *scaler, *ctrl, *filter, *sink;
    GstCaps *caps;
    unsigned int hinput = state->hres;
    unsigned int vinput = state->vres;
    unsigned int houtput, voutput;
    unsigned int hoffset, voffset;
    unsigned int scale_mul = 1, scale_div = 1;

    queue =     gst_element_factory_make("queue",           "lcdqueue");
    scaler =    gst_element_factory_make("omx_mdeiscaler",  "lcdscaler");
    ctrl =      gst_element_factory_make("omx_ctrl",        "lcdctrl");
    filter =    gst_element_factory_make("capsfilter",      "lcdcaps");
    sink =      gst_element_factory_make("omx_videosink",   "lcdsink");
    if (!queue || !scaler || !ctrl || !sink) {
        return NULL;
    }

	g_object_set(G_OBJECT(sink), "sync", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "colorkey", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(sink), "display-device", "LCD", NULL);
	g_object_set(G_OBJECT(ctrl), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(ctrl), "display-device", "LCD", NULL);

	gst_bin_add_many(GST_BIN(state->pipeline), queue, scaler, ctrl, filter, sink, NULL);

    /* Sanity-check and apply the crop configuration. */
    fprintf(stderr, "DEBUG: crop = %u,%u@%ux%u\n", state->startx, state->starty, state->cropx, state->cropy);
    while (state->cropx && state->cropy) {
        char crop[64];
        if ((state->startx + state->cropx) > state->hres) break;
        if ((state->starty + state->cropy) > state->vres) break;
        hinput = state->cropx;
        vinput = state->cropy;
        sprintf(crop, "%u,%u@%ux%u", state->startx, state->starty, hinput, vinput);
        g_object_set(G_OBJECT(scaler), "crop-area", crop, NULL);
        break;
    }

    /* Configure the output offset on the LCD. */
    if ((output->hres * vinput) > (output->vres * hinput)) {
        scale_mul = output->vres;
        scale_div = vinput;
    }
    else {
        scale_mul = output->hres;
        scale_div = hinput;
    }
    houtput = ((hinput * scale_mul) / scale_div) & ~0xF;
    voutput = ((vinput * scale_mul) / scale_div) & ~0x1;
    hoffset = (output->xoff + (output->hres - houtput) / 2) & ~0x1;
    voffset = (output->yoff + (output->vres - voutput) / 2) & ~0x1;

#if 1
    fprintf(stderr, "DEBUG: scale = %u/%u\n", scale_mul, scale_div);
    fprintf(stderr, "DEBUG: input = [%u, %u]\n", hinput, vinput);
    fprintf(stderr, "DEBUG: output = [%u, %u]\n", houtput, voutput);
    fprintf(stderr, "DEBUG: offset = [%u, %u]\n", hoffset, voffset);
#endif
	g_object_set(G_OBJECT(sink), "top", (guint)voffset, NULL);
	g_object_set(G_OBJECT(sink), "left", (guint)hoffset, NULL);

    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "width", G_TYPE_INT, houtput,
                "height", G_TYPE_INT, voutput,
                NULL);
    g_object_set(G_OBJECT(filter), "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Link LCD Output capabilities. */
    ret = gst_element_link_pads(scaler, "src_00", ctrl, "sink");
    if (!ret) {
        gst_object_unref(GST_OBJECT(state->pipeline));
        return NULL;
    }

    /* Return the first element of our segment to link with */
    gst_element_link_many(queue, scaler, NULL);
    gst_element_link_many(ctrl, filter, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
}

void
cam_lcd_reconfig(struct pipeline_state *state, const struct display_config *output)
{
    GstElement *filter = gst_bin_get_by_name(GST_BIN(state->pipeline), "lcdcaps");
    GstElement *sink = gst_bin_get_by_name(GST_BIN(state->pipeline), "lcdsink");
    GstCaps *caps;
    unsigned int hout, vout;
    unsigned int hoff, voff;
    unsigned int scale_mul = 1, scale_div = 1;

    if (!filter || !sink) {
        return;
    }

    /* Recompute the scaler parameters. */
    if ((output->hres * state->vres) > (output->vres * state->hres)) {
        scale_mul = output->vres;
        scale_div = state->vres;
    }
    else {
        scale_mul = output->hres;
        scale_div = state->hres;
    }
    hout = ((state->hres * scale_mul) / scale_div) & ~0xF;
    vout = ((state->vres * scale_mul) / scale_div) & ~0x1;
    hoff = (output->xoff + (output->hres - hout) / 2) & ~0x1;
    voff = (output->yoff + (output->vres - vout) / 2) & ~0x1;

    /* Set the new configuration */
    g_object_set(G_OBJECT(sink), "top", (guint)voff, NULL);
    g_object_set(G_OBJECT(sink), "left", (guint)hoff, NULL);
    caps = gst_caps_new_simple ("video/x-raw-yuv",
                "width", G_TYPE_INT, hout,
                "height", G_TYPE_INT, vout,
                NULL);
    g_object_set(G_OBJECT(filter), "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Pause and restart the pipeline */
    cam_pipeline_restart(state);
}
