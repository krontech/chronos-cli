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
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <gst/gst.h>
#include <sys/mman.h>

#include "pipeline.h"
#include "utils.h"

static void
debug_log16(const void *data)
{
    unsigned int i;
    const uint16_t *pdata = data;
    for (i = 0; i < 16; i++) {
        fprintf(stderr, " 0x%04x", pdata[i]);
    }
    fprintf(stderr, "\n");
}

static gboolean
blackref_probe(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    int size = GST_BUFFER_SIZE(buffer);

    if (state->phantom) {
        state->phantom--;
        return FALSE;
    }
    memcpy_sum16(state->scratchpad, GST_BUFFER_DATA(buffer), size);
    return TRUE;
}

static void
cam_blackref_done(struct pipeline_state *state, const struct pipeline_args *args)
{
    int fd = open(args->filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd >= 0) {
        neon_div16(state->scratchpad, state->hres * state->vres * 2);
        write(fd, state->scratchpad, state->hres * state->vres * 2);
        close(fd);
    }
}

/* Launch a gstreamer pipeline to take a black reference image. */
GstElement *
cam_blackref(struct pipeline_state *state, struct pipeline_args *args)
{
    gboolean ret;
    GstElement *queue, *sink;
    GstPad *pad;
    GstCaps *caps;
    unsigned long size = state->hres * state->vres * 2;
    uint16_t *refdata;

    /* Open the file for writing. */
    state->phantom = 1;

    /* Build the GStreamer Pipeline */
    state->pipeline = gst_pipeline_new ("pipeline");
    state->source   = gst_element_factory_make("omx_camera",    "vfcc-source");
    queue           = gst_element_factory_make("queue",         "ref-queue");
    sink            = gst_element_factory_make("fakesink",      "file-sink");
    if (!state->pipeline || !state->source || !queue || !sink) {
        return NULL;
    }
    gst_bin_add_many(GST_BIN(state->pipeline), state->source, queue, sink, NULL);

    /* Configure elements. */
    g_object_set(G_OBJECT(state->source), "input-interface", "VIP1_PORTA", NULL);
    g_object_set(G_OBJECT(state->source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
    g_object_set(G_OBJECT(state->source), "vif-mode", "24BIT", NULL);
    g_object_set(G_OBJECT(state->source), "output-buffers", (guint)10, NULL);
    g_object_set(G_OBJECT(state->source), "skip-frames", (guint)0, NULL);
    g_object_set(G_OBJECT(state->source), "num-buffers", (guint)(16 + state->phantom), NULL);

    /* Configure the interface for 16-bit greyscale. */
    caps = gst_caps_new_simple ("video/x-raw-gray",
                "bpp", G_TYPE_INT, 16,
                "width", G_TYPE_INT, state->hres,
                "height", G_TYPE_INT, state->vres,
                "framerate", GST_TYPE_FRACTION, LIVE_MAX_FRAMERATE, 1,
                "buffer-count-requested", G_TYPE_INT, 4,
                NULL);
    ret = gst_element_link_filtered(state->source, queue, caps);
    if (!ret) {
        gst_object_unref(GST_OBJECT(state->pipeline));
        return NULL;
    }
    gst_caps_unref(caps);

    /* Configure the file sink */
    pad = gst_element_get_static_pad(queue, "src");
    gst_pad_add_buffer_probe(pad, G_CALLBACK(blackref_probe), state);
    gst_object_unref(pad);

    /* Require a custom event on EOF to finalize the file. */
    state->done = cam_blackref_done;

    gst_element_link_many(queue, sink, NULL);
    return state->pipeline;
}

