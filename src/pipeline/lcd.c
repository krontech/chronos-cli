
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

static void
cam_lcd_scaler_setup(struct pipeline_state *state, const struct display_config *output,
    GstElement *scaler, GstElement *filter, GstElement *sink)
{
    GstCaps *caps;

    char crop[64];
    unsigned int cropx = state->source.hres;
    unsigned int cropy = state->source.vres;
    unsigned int startx = 0;
    unsigned int starty = 0;
    unsigned int scale_mul, scale_div;
    unsigned int houtput, voutput;
    unsigned int hoffset, voffset;

    /* Determine the nominal scale ratio. */
    if ((output->hres * state->source.vres) > (output->vres * state->source.hres)) {
        scale_mul = output->vres;
        scale_div = state->source.vres;
    }
    else {
        scale_mul = output->hres;
        scale_div = state->source.hres;
    }
    /* Apply digital zoom */
    if (state->config.video_zoom >= 1.0) {
        scale_mul *= state->config.video_zoom;
    } else {
        scale_div /= state->config.video_zoom;
    }

    /* Compute the actual output video size. */
    houtput = ((state->source.hres * scale_mul) / scale_div) & ~0xF;
    voutput = ((state->source.vres * scale_mul) / scale_div) & ~0x1;
    if (houtput > output->hres) {
        /* It's unclear if the input cropper has quantization requirements. */
        cropx = ((state->source.hres * output->hres) / houtput) & ~0x1;
        startx = ((state->source.hres - cropx) / 2) & ~0x1;
        houtput = output->hres & ~0xF;
    }
    if (voutput > output->vres) {
        cropy = ((state->source.vres * output->vres) / voutput) & ~0x1;
        starty = ((state->source.vres - cropy) / 2) & ~0x1;
        voutput = output->vres & ~0x1;
    }
    sprintf(crop, "%u,%u@%ux%u", startx, starty, cropx, cropy);
    g_object_set(G_OBJECT(scaler), "crop-area", crop, NULL);
    hoffset = (output->xoff + (output->hres - houtput) / 2) & ~0x1;
    voffset = (output->yoff + (output->vres - voutput) / 2) & ~0x1;

#ifdef DEBUG
    fprintf(stderr, "DEBUG: crop = %s\n", crop);
    fprintf(stderr, "DEBUG: scale = %u/%u\n", scale_mul, scale_div);
    fprintf(stderr, "DEBUG: input = [%u, %u]\n", state->source.hres, state->source.vres);
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
}

GstPad *
cam_lcd_sink(struct pipeline_state *state, const struct display_config *output)
{
    gboolean ret;
    GstElement *queue, *scaler, *ctrl, *filter, *sink;

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

    /* Configure the LCD and scaler setup */
    cam_lcd_scaler_setup(state, output, scaler, filter, sink);

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
    GstElement *scaler = gst_bin_get_by_name(GST_BIN(state->pipeline), "lcdscaler");
    GstElement *filter = gst_bin_get_by_name(GST_BIN(state->pipeline), "lcdcaps");
    GstElement *sink = gst_bin_get_by_name(GST_BIN(state->pipeline), "lcdsink");

    if (scaler && filter && sink) {
        /* Update the scaler configuration. */
        cam_lcd_scaler_setup(state, output, scaler, filter, sink);

        /* Pause and restart the pipeline - because caps renegotiation doesn't work. */
        cam_pipeline_restart(state);
    }
}
