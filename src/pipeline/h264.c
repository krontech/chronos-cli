
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
#include <fcntl.h>
#include <setjmp.h>
#include <gst/gst.h>

#include "pipeline.h"

GstPad *
cam_h264_sink(struct pipeline_state *state, GstElement *pipeline)
{
    GstElement *encoder, *queue, *parser, *mux, *sink;
    unsigned int minrate = (state->hres * state->yres * state->encrate / 4); /* Set a minimum quality of 0.25 bpp. */
    int fd;

    /* Open the file for writing. */
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
        return NULL;
	}

    /* Allocate our segment of the video pipeline. */
    encoder =	gst_element_factory_make("omx_h264enc",	    "h264-encoder");
    queue =		gst_element_factory_make("queue",		    "h264-queue");
    parser =	gst_element_factory_make("rr_h264parser",   "h264-parser");
    mux =		gst_element_factory_make("mp4mux",			"mp4-mux");
    sink =		gst_element_factory_make("fdsink",			"file-sink");
    if (!encoder || !queue || !parser || !mux || !sink) {
        close(fd);
        return NULL;
    }

    /* Enforce maximum and maximum bitrates for the encoder. */
    if (state->bitrate > 60000000UL) state->bitrate = 60000000UL;
    else if (state->bitrate < minrate) state->bitrate = minrate;

    /* Configure the H.264 Encoder */
    g_object_set(G_OBJECT(encoder), "force-idr-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "i-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "bitrate", (guint)state->bitrate, NULL);
    g_object_set(G_OBJECT(encoder), "profile", (guint)OMX_H264ENC_PROFILE_HIGH, NULL);
    g_object_set(G_OBJECT(encoder), "level", (guint)OMX_H264ENC_LVL_51, NULL);
    g_object_set(G_OBJECT(encoder), "encodingPreset", (guint)OMX_H264ENC_ENC_PRE_HSMQ, NULL);
    g_object_set(G_OBJECT(encoder), "framerate", (guint)state->encrate, NULL);

    /* Configure the H.264 Parser. */
	g_object_set(G_OBJECT(parser), "singleNalu", (gboolean)TRUE, NULL);

    /* Configure the MPEG-4 Multiplexer */
    g_object_set(G_OBJECT(mux), "dts-method", (guint)0, NULL);

	gst_bin_add_many(GST_BIN(pipeline), encoder, queue, parser, mux, sink, NULL);

    /* Return the first element of our segment to link with */
    gst_element_link_many(encoder, queue, parser, mux, sink, NULL);
    return gst_element_get_static_pad(encoder, "sink");
}
