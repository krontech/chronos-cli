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

#include "pipeline.h"

enum
{
	OMX_H264ENC_PROFILE_BASE =		1,
	OMX_H264ENC_PROFILE_MAIN =		2,
	OMX_H264ENC_PROFILE_EXTENDED =	4,
	OMX_H264ENC_PROFILE_HIGH =		8,
	OMX_H264ENC_PROFILE_HIGH_10 =	16,
	OMX_H264ENC_PROFILE_HIGH_422 =	32,
	OMX_H264ENC_PROFILE_HIGH_444 =	64
};

enum
{
	OMX_H264ENC_LVL_1 =		1,
	OMX_H264ENC_LVL_1B =	2,
	OMX_H264ENC_LVL_11 =	4,
	OMX_H264ENC_LVL_12 =	8,
	OMX_H264ENC_LVL_13 =	16,
	OMX_H264ENC_LVL_2 =		32,
	OMX_H264ENC_LVL_21 =	64,
	OMX_H264ENC_LVL_22 =	128,
	OMX_H264ENC_LVL_3 =		256,
	OMX_H264ENC_LVL_31 =	512,
	OMX_H264ENC_LVL_32 =	1024,
	OMX_H264ENC_LVL_4 =		2048,
	OMX_H264ENC_LVL_41 =	4096,
	OMX_H264ENC_LVL_42 =	8192,
	OMX_H264ENC_LVL_5 =		16384,
	OMX_H264ENC_LVL_51 =	32768
};

enum
{
	OMX_H264ENC_ENC_PRE_HQ =	1,
	OMX_H264ENC_ENC_PRE_USER =	2,
	OMX_H264ENC_ENC_PRE_HSMQ =	3,
	OMX_H264ENC_ENC_PRE_MSMQ =	4,
	OMX_H264ENC_ENC_PRE_MSHQ =	5,
	OMX_H264ENC_ENC_PRE_HS =	6
};

GstPad *
cam_h264_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *encoder, *queue, *parser, *mux, *sink;
    unsigned int minrate = (state->hres * state->vres * args->framerate / 4); /* Set a minimum quality of 0.25 bpp. */
    int flags = O_RDWR | O_CREAT | O_TRUNC;

#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif

    /* Open the file for writing. */
	state->write_fd = open(args->filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (state->write_fd < 0) {
        fprintf(stderr, "Unable to open %s for writing (%s)\n", args->filename, strerror(errno));
        return NULL;
	}

    /* Allocate our segment of the video pipeline. */
    encoder =	gst_element_factory_make("omx_h264enc",	    "h264-encoder");
    queue =		gst_element_factory_make("queue",		    "h264-queue");
    parser =	gst_element_factory_make("rr_h264parser",   "h264-parser");
    mux =		gst_element_factory_make("mp4mux",			"mp4-mux");
    sink =		gst_element_factory_make("fdsink",			"file-sink");
    if (!encoder || !queue || !parser || !mux || !sink) {
        close(state->write_fd);
        return NULL;
    }

    /* Enforce maximum and maximum bitrates for the encoder. */
    if (args->bitrate > 60000000UL) args->bitrate = 60000000UL;
    else if (args->bitrate < minrate) args->bitrate = minrate;

    /* Configure the H.264 Encoder */
    g_object_set(G_OBJECT(encoder), "force-idr-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "i-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "bitrate", (guint)args->bitrate, NULL);
    g_object_set(G_OBJECT(encoder), "profile", (guint)OMX_H264ENC_PROFILE_HIGH, NULL);
    g_object_set(G_OBJECT(encoder), "level", (guint)OMX_H264ENC_LVL_51, NULL);
    g_object_set(G_OBJECT(encoder), "encodingPreset", (guint)OMX_H264ENC_ENC_PRE_HSMQ, NULL);
    g_object_set(G_OBJECT(encoder), "framerate", (guint)args->framerate, NULL);

    /* Configure the H.264 Parser. */
	g_object_set(G_OBJECT(parser), "singleNalu", (gboolean)TRUE, NULL);

    /* Configure the MPEG-4 Multiplexer */
    g_object_set(G_OBJECT(mux), "dts-method", (guint)0, NULL);

    /* Configure the file sink */
    g_object_set(G_OBJECT(sink), "fd", (gint)state->write_fd, NULL);

    /* Return the first element of our segment to link with */
    gst_bin_add_many(GST_BIN(state->pipeline), encoder, queue, parser, mux, sink, NULL);
    gst_element_link_many(encoder, queue, parser, mux, sink, NULL);
    return gst_element_get_static_pad(encoder, "sink");
}
