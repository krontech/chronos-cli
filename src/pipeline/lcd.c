
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
cam_lcd_sink(GstElement *pipeline, unsigned long hsrc, unsigned long vsrc, const struct display_config *config)
{
    gboolean ret;
    GstElement *queue, *scaler, *ctrl, *sink;
    GstCaps *caps;
    unsigned int hout, vout;
    unsigned int hoff, voff;
    unsigned int scale_mul = 1, scale_div = 1;

    queue =     gst_element_factory_make("queue",           "lcdqueue");
    scaler =    gst_element_factory_make("omx_mdeiscaler",  "lcdscaler");
    ctrl =      gst_element_factory_make("omx_ctrl",        "lcdctrl");
    sink =      gst_element_factory_make("omx_videosink",   "lcdsink");
    if (!queue || !scaler || !ctrl || !sink) {
        return NULL;
    }

	g_object_set(G_OBJECT(sink), "sync", (gboolean)0, NULL);
	g_object_set(G_OBJECT(sink), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(sink), "display-device", "LCD", NULL);
	g_object_set(G_OBJECT(ctrl), "display-mode", "OMX_DC_MODE_1080P_60", NULL);
	g_object_set(G_OBJECT(ctrl), "display-device", "LCD", NULL);

	gst_bin_add_many(GST_BIN(pipeline), queue, scaler, ctrl, sink, NULL);

    if ((config->hres * vsrc) > (config->vres * hsrc)) {
        scale_mul = config->vres;
        scale_div = vsrc;
    }
    else {
        scale_mul = config->hres;
        scale_div = hsrc;
    }
    hout = ((hsrc * scale_mul) / scale_div) & ~0xF;
    vout = ((vsrc * scale_mul) / scale_div) & ~0x1;
    hoff = (config->xoff + (config->hres - hout) / 2) & ~0x1;
    voff = (config->yoff + (config->vres - vout) / 2) & ~0x1;

#ifdef DEBUG
    fprintf(stderr, "DEBUG: scale = %u/%u\n", scale_mul, scale_div);
    fprintf(stderr, "DEBUG: input = [%u, %u]\n", hsrc, vsrc);
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
