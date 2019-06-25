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

enum
{
    OMX_H264ENC_RATE_LOW_DELAY = 0,
    OMX_H264ENC_RATE_STORAGE =   1,
    OMX_H264ENC_RATE_TWO_PASS =  2,
    OMX_H265ENC_RATE_NONE =      3
};

GstPad *
cam_h264_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *encoder, *queue, *neon, *parser, *mux, *sink;
    unsigned int minrate = (state->source.hres * state->source.vres * args->framerate / 4); /* Set a minimum quality of 0.25 bpp. */
    int flags = O_RDWR | O_CREAT | O_TRUNC;

#if defined(O_LARGEFILE)
    flags |= O_LARGEFILE;
#elif defined(__O_LARGEFILE)
    flags |= __O_LARGEFILE;
#endif

    /* Open the file for writing. */
    state->write_fd = open(args->filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (state->write_fd < 0) {
        fprintf(stderr, "Unable to open %s for writing (%s)\n", args->filename, strerror(errno));
        return NULL;
    }

    /* Allocate our segment of the video pipeline. */
    encoder = gst_element_factory_make("omx_h264enc", "h264-encoder");
    queue =   gst_element_factory_make("queue",       "h264-queue");
    neon =    gst_element_factory_make("neon",        "h264-neon");
    parser =  gst_element_factory_make("h264parse",   "h264-parser");
    mux =     gst_element_factory_make("mp4mux",      "mp4-mux");
    sink =    gst_element_factory_make("fdsink",      "file-sink");
    if (!encoder || !queue || !neon || !parser || !mux || !sink) {
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
    g_object_set(G_OBJECT(parser), "split-packetized", (gboolean)FALSE, NULL);
    g_object_set(G_OBJECT(parser), "access-unit", (gboolean)TRUE, NULL);
    g_object_set(G_OBJECT(parser), "output-format", (guint)0, NULL);

    /* Configure the MPEG-4 Multiplexer */
    g_object_set(G_OBJECT(mux), "dts-method", (guint)0, NULL);

    /* Configure the file sink */
    g_object_set(G_OBJECT(sink), "fd", (gint)state->write_fd, NULL);

    /* Return the first element of our segment to link with */
    gst_bin_add_many(GST_BIN(state->pipeline), encoder, queue, neon, parser, mux, sink, NULL);
    gst_element_link_many(encoder, queue, neon, parser, mux, sink, NULL);
    return gst_element_get_static_pad(encoder, "sink");
}

GstPad *
cam_network_sink(struct pipeline_state *state)
{
    GstElement *encoder, *queue, *parser, *neon, *mux, *sink;

    /* Allocate our segment of the video pipeline. */
    queue =   gst_element_factory_make("queue",       "net-queue");
    encoder = gst_element_factory_make("omx_h264enc", "net-encoder");
    neon =    gst_element_factory_make("neon",        "net-neon");
    parser =  gst_element_factory_make("h264parse",   "net-parse");
    mux =     gst_element_factory_make("mpegtsmux",   "net-mux");
    sink =    gst_element_factory_make("tcpserversink", "net-sink");
    if (!queue || !encoder || !neon || !parser || !mux || !sink) {
        return NULL;
    }

    /* Configure the H264 encoder for low-latency low-birate operation. */
    g_object_set(G_OBJECT(encoder), "force-idr-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "i-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "bitrate", (guint)500000, NULL);
    g_object_set(G_OBJECT(encoder), "profile", (guint)OMX_H264ENC_PROFILE_HIGH, NULL);
    g_object_set(G_OBJECT(encoder), "level", (guint)OMX_H264ENC_LVL_51, NULL);
    g_object_set(G_OBJECT(encoder), "encodingPreset", (guint)OMX_H264ENC_ENC_PRE_HSMQ, NULL);
    g_object_set(G_OBJECT(encoder), "rateControlPreset", (guint)OMX_H264ENC_RATE_LOW_DELAY, NULL);
    g_object_set(G_OBJECT(encoder), "framerate", (guint)LIVE_MAX_FRAMERATE/2, NULL);
    
    g_object_set(G_OBJECT(sink), "recover-policy", (guint)3, NULL); /* Recover by syncing to last keyframe. */
    g_object_set(G_OBJECT(sink), "sync-method", (guint)2, NULL);    /* Sync clients from the last keyframe. */
    g_object_set(G_OBJECT(sink), "port", (guint)NETWORK_STREAM_PORT, NULL);

    gst_bin_add_many(GST_BIN(state->pipeline), queue, encoder, neon, parser, mux, sink, NULL);
    gst_element_link_many(queue, encoder, neon, parser, mux, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
}

GstPad *
cam_liverec_sink(struct pipeline_state *state)
{

    /* Declare Video Elements */
    GstElement *encoder, *queue, *parser, *neon, *mux, *sink;

    /* Declare Audio Elements */
    GstElement *soundsource, *soundcapsfilt, *soundqueue, *soundconverter, *soundratesync, *soundresample; 
    GstCaps *caps;

    char liverec_file_name[64];
    time_t curtime;
    struct tm *loc_time;

    /* Create a string with the save path and filename with current timestamp */
    curtime = time (NULL);
    loc_time = localtime (&curtime);
    strftime (liverec_file_name, 64, "/tmp/live_%F_%H-%M-%S.mp4", loc_time);
    fprintf(stderr, "%s\n", liverec_file_name);

    state->liverec_fd = open(liverec_file_name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(state->liverec_fd < 0) {
        fprintf(stderr,"Unable to open %s for writing (%s)\n", liverec_file_name, strerror(errno));
    }

    /* Allocate our segment of the video pipeline. */
    encoder = gst_element_factory_make("omx_h264enc", "liverec-encoder");
    queue =   gst_element_factory_make("queue",       "liverec-queue");
    neon =    gst_element_factory_make("neon",        "liverec-neon");
    parser =  gst_element_factory_make("h264parse",   "liverec-parse");
    mux =     gst_element_factory_make("qtmux",       "liverec-mux");
    sink =    gst_element_factory_make("fdsink",      "liverec-file-sink");

    if (!encoder || !queue || !neon || !parser || !mux || !sink) {
        close(state->liverec_fd);
        return NULL;
    }

    /* Configure the H.264 Encoder */
    g_object_set(G_OBJECT(encoder), "force-idr-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "i-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "bitrate", (guint)6000000UL, NULL); //TODO: 6 Mbps -> Usable but can be discussed
    g_object_set(G_OBJECT(encoder), "profile", (guint)OMX_H264ENC_PROFILE_HIGH, NULL);
    g_object_set(G_OBJECT(encoder), "level", (guint)OMX_H264ENC_LVL_51, NULL);
    g_object_set(G_OBJECT(encoder), "encodingPreset", (guint)OMX_H264ENC_ENC_PRE_HSMQ, NULL);
    g_object_set(G_OBJECT(encoder), "framerate", (guint)60, NULL);

    /* Configure the MPEG-4 Multiplexer */
    g_object_set(G_OBJECT(mux), "dts-method", (guint)0, NULL);

    /* Allocate our segment of the audio pipeline */
    soundsource =    gst_element_factory_make("alsasrc",       "liverec-alsasrc");
    soundcapsfilt =  gst_element_factory_make("capsfilter",    "liverec-capsfilter");
    soundqueue =     gst_element_factory_make("queue",         "liverec-soundqueue");
    soundconverter = gst_element_factory_make("audioconvert",  "liverec-soundconverter");
    soundresample =  gst_element_factory_make("audioresample", "liverec-soundresample");
    soundratesync =  gst_element_factory_make("audiorate",     "liverec-soundrate");

    if(!soundsource || !soundcapsfilt || !soundqueue || !soundconverter || !soundresample || !soundratesync){
        close(state->liverec_fd);
        return NULL;
    }

    /* Disable buffering in the sound queue */
    g_object_set(G_OBJECT(soundqueue), "max-size-buffers", (guint) 0, NULL);
    g_object_set(G_OBJECT(soundqueue), "max-size-bytes", (guint) 0, NULL);
    g_object_set(G_OBJECT(soundqueue), "max-size-time", (guint) 0, NULL);

    /* Configure a capabilities filter for alsasrc */
    caps = gst_caps_new_simple( "audio/x-raw-int",
                                "channels", G_TYPE_INT, 1,
                                "width",    G_TYPE_INT, 16,
                                "depth",    G_TYPE_INT, 16,
                                "rate",     G_TYPE_INT, 48000,
                                "signed",   G_TYPE_BOOLEAN, TRUE,
                                NULL);

    g_object_set(G_OBJECT(soundcapsfilt), "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Configure the file sink */
    g_object_set(G_OBJECT(sink), "fd", (gint)state->liverec_fd, NULL);
    g_object_set (G_OBJECT (sink), "sync", FALSE, NULL);

    /* Return the first element of our segment to link with */
    gst_bin_add_many(GST_BIN(state->pipeline), encoder, queue, neon, parser, mux, sink, soundsource, soundcapsfilt, soundqueue, soundconverter, soundresample, soundratesync, NULL);

    /* Link video elements to mp4mux */
    gst_element_link_many(encoder, queue, neon, parser, mux, NULL);

    /* Link audio elements to mp4mux */
    gst_element_link_many(soundsource, soundcapsfilt, soundqueue, soundconverter, soundresample, soundratesync, mux, NULL);

    /* Link mp4mux to a file sink */
    gst_element_link_many(mux, sink, NULL);

    return gst_element_get_static_pad(encoder, "sink");
}