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
#include <gst/gst.h>
#include <arpa/inet.h>

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

    /* Configure the H.264 Parser */
#if !GST_CHECK_VERSION(0,10,36)
    g_object_set(G_OBJECT(parser), "split-packetized", (gboolean)FALSE, NULL);
    g_object_set(G_OBJECT(parser), "access-unit", (gboolean)TRUE, NULL);
    g_object_set(G_OBJECT(parser), "output-format", (guint)0, NULL);
#endif

    /* Configure the MPEG-4 Multiplexer */
    g_object_set(G_OBJECT(mux), "dts-method", (guint)0, NULL);

    /* Configure the file sink */
    g_object_set(G_OBJECT(sink), "fd", (gint)state->write_fd, NULL);

    /* Return the first element of our segment to link with */
    gst_bin_add_many(GST_BIN(state->pipeline), encoder, queue, neon, parser, mux, sink, NULL);
    gst_element_link_many(encoder, queue, neon, parser, mux, sink, NULL);
    return gst_element_get_static_pad(encoder, "sink");
}

#if ENABLE_RTSP_SERVER
static void
cam_network_sink_add_host(const struct rtsp_session *sess, void *closure)
{
    GstElement *sink = closure;
    if (sess->state == RTSP_SESSION_PLAY) {
        g_signal_emit_by_name(G_OBJECT(sink), "add", sess->host, sess->port, NULL);
    }
}

static void
cam_network_sink_update(const struct rtsp_session *sess, void *closure)
{
    GstElement *sink = closure;
    if (sess->state == RTSP_SESSION_PLAY) {
        g_signal_emit_by_name(G_OBJECT(sink), "add", sess->host, sess->port, NULL);
    }
    else {
        g_signal_emit_by_name(G_OBJECT(sink), "remove", sess->host, sess->port, NULL);
    }
}

static GstPad *
cam_rtsp_sink(struct pipeline_state *state)
{
    GstElement *queue, *parser, *payload, *sink;
    struct in_addr addr;

    /* Allocate our segment of the video pipeline. */
    queue =   gst_element_factory_make("queue",       "net-queue");
    parser =  gst_element_factory_make("h264parse",   "net-parse");
    payload = gst_element_factory_make("rtph264pay",  "net-payload");
    sink =    gst_element_factory_make("multiudpsink", "net-sink");
    if (!queue || !parser || !payload || !sink) {
        return NULL;
    }

    /* Register RTSP clients. */
    rtsp_session_foreach(state->rtsp, cam_network_sink_add_host, sink);
    rtsp_server_set_hook(state->rtsp, cam_network_sink_update, sink);

    gst_bin_add_many(GST_BIN(state->pipeline), queue, parser, payload, sink, NULL);
    gst_element_link_many(queue, parser, payload, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
}
#endif /* ENABLE_RTSP_SERVER */

static GstPad *
cam_liverec_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *mux, *sink;      /* MPEG-4 pipeline segment */
    GstElement *queue, *parser; /* H.264 pipeline segment. */
    GstElement *soundsource, *soundcapsfilt, *soundprequeue, *soundqueue, *soundencoder, *soundparser, *soundrate;
    GstCaps *caps;

    char timestampStr[32];
    char scratchStr[PATH_MAX] = {'\0'};
    time_t curtime;
    struct tm *loc_time;
    int flags = O_RDWR | O_CREAT | O_TRUNC;
#if defined(O_LARGEFILE)
    flags |= O_LARGEFILE;
#elif defined(__O_LARGEFILE)
    flags |= __O_LARGEFILE;
#endif

    /* Do nothing if we are not in live display mode, or there is nothing configured */
    if ((args->mode != PLAYBACK_STATE_LIVE) || (!args->liverecord)) {
        return NULL;
    }

    /* Create a string with the save path and filename with current timestamp */
    if (args->multifile){
        curtime = time (NULL);
        loc_time = localtime (&curtime);
        strftime (timestampStr, 32, "_%F_%H-%M-%S.mp4", loc_time);
        strcat(scratchStr, state->args.live_filename);
        strcat(scratchStr, timestampStr);
        strcpy(state->liverec_filename, scratchStr);
    } else {
        strcpy(state->liverec_filename, state->args.live_filename);
    }

    /* Set the filename as specified by the user. */
    state->liverec_fd = open(state->liverec_filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (state->liverec_fd < 0) {
        fprintf(stderr,"Unable to open %s for writing (%s)\n", state->liverec_filename, strerror(errno));
        return NULL;
    } else {
        fprintf(stderr,"Live Record file will be saved to %s\n", state->liverec_filename);
    }

    /* Allocate our segment of the video pipeline. */
    queue =   gst_element_factory_make("queue",       "liverec-queue");
    parser =  gst_element_factory_make("h264parse",   "liverec-parse");
    mux =     gst_element_factory_make("mp4mux",      "liverec-mux");
    sink =    gst_element_factory_make("fdsink",      "liverec-file-sink");
    if (!queue || !parser || !mux || !sink) {
        close(state->liverec_fd);
        return NULL;
    }
    gst_bin_add_many(GST_BIN(state->pipeline), queue, parser, mux, sink, NULL);

    /* Configure the MPEG-4 Multiplexer */
    g_object_set(G_OBJECT(mux), "dts-method", (guint)0, NULL);

    /* Link video elements to mp4mux */
    gst_element_link_many(queue, parser, mux, NULL);

    /* Allocate the audio pipeline */
    soundsource =    gst_element_factory_make("alsasrc",       "liverec-alsasrc");
    soundcapsfilt =  gst_element_factory_make("capsfilter",    "liverec-capsfilter");
    soundprequeue =  gst_element_factory_make("queue",         "liverec-soundprequeue");
    soundencoder =   gst_element_factory_make("omx_aacenc",    "liverec-soundencoder");
    soundqueue =     gst_element_factory_make("queue",         "liverec-soundqueue");
    soundparser =    gst_element_factory_make("aacparse",      "liverec-soundparser");
    if (!soundsource || !soundcapsfilt || !soundprequeue || !soundqueue || !soundencoder || !soundparser){
        close(state->liverec_fd);
        return NULL;
    }
    gst_bin_add_many(GST_BIN(state->pipeline), soundsource, soundcapsfilt, soundprequeue, soundqueue, soundencoder, soundparser, NULL);

    /* Configure the ALSA sound source */
    g_object_set(G_OBJECT(soundsource), "buffer-time", (gint64)800000, NULL);
    g_object_set(G_OBJECT(soundsource), "latency-time", (gint64)20000, NULL);
    g_object_set(G_OBJECT(soundsource), "provide-clock", (gboolean)FALSE, NULL);
    g_object_set(G_OBJECT(soundsource), "slave-method", (guint)0, NULL);
    g_object_set(G_OBJECT(soundsource), "do-timestamp", (gboolean)TRUE, NULL);

    /* Configure sound prequeue */
    g_object_set(G_OBJECT(soundprequeue), "max-size-bytes",     (guint)4294967294, NULL);
    g_object_set(G_OBJECT(soundprequeue), "max-size-time",      (guint64)1844674407370955161, NULL);
    g_object_set(G_OBJECT(soundprequeue), "max-size-buffers",   (guint)4294967294, NULL);

    /* Configure the omx aac encoder */
    g_object_set(G_OBJECT(soundencoder), "output-format", (gint)4, NULL);

    /* Configure the audio format for the ALSA source. */
    caps = gst_caps_new_simple( "audio/x-raw-int",
                                "channels", G_TYPE_INT, 2,
                                "width",    G_TYPE_INT, 16,
                                "depth",    G_TYPE_INT, 16,
                                "rate",     G_TYPE_INT, 48000,
                                "signed",   G_TYPE_BOOLEAN, TRUE,
                                NULL);

    /* Link audio elements to mp4mux */
    gst_element_link_filtered(soundsource, soundprequeue, caps);
    gst_element_link_many(soundprequeue, soundencoder, soundqueue, soundparser, mux, NULL);
    gst_caps_unref(caps);

    /* Link and configure the mp4mux to its file sink */
    g_object_set(G_OBJECT(sink), "fd", (gint)state->liverec_fd, NULL);
    g_object_set(G_OBJECT(sink), "sync", FALSE, NULL);
    gst_element_link_many(mux, sink, NULL);

    /* Create a thread to monitor the file size. */
    pthread_create(&state->liverec_sizemon, NULL, liverec_size_monitor, state);

    return gst_element_get_static_pad(queue, "sink");
}

/* Monitor filesize of a live recording on a seperate thread. */
static void *
liverec_size_monitor(void *data)
{

    struct pipeline_state *state = data;
    struct stat *buf;
    double currentFileSize = 0;

    buf = malloc(sizeof(struct stat));    

    while(state->args.liverecord){
        stat(state->liverec_filename, buf);
        currentFileSize = buf->st_size;

        if(currentFileSize >= state->args.maxFilesize){
            fprintf(stderr,"liverec_size_monitor: max filesize reached\n");

            /* Gracefully stop and restart the liverec element */
            if (state->playstate != PLAYBACK_STATE_FILESAVE) {
                currentFileSize = 0;
                cam_pipeline_restart(state);
                break;
            }

        }
        
        sleep(1);
    
    }
    
    free(buf);
    pthread_exit(NULL);
}

GstPad *
cam_h264_live_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *queue, *encoder, *neon, *tee;
    GstPad *sinkpad;

    /* Allocate our segment of the video pipeline. */
    queue =   gst_element_factory_make("queue",       "h264-queue");
    encoder = gst_element_factory_make("omx_h264enc", "h264-encoder");
    neon =    gst_element_factory_make("neon",        "h264-neon");
    tee =     gst_element_factory_make("tee",         "h264-tee");
    if (!queue || !encoder || !neon || !tee) {
        return NULL;
    }

    /* Configure the H264 encoder for low-latency medium-birate operation. */
    g_object_set(G_OBJECT(encoder), "force-idr-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "i-period", (guint)90, NULL);
    g_object_set(G_OBJECT(encoder), "bitrate", (guint)5000000, NULL);
    g_object_set(G_OBJECT(encoder), "profile", (guint)OMX_H264ENC_PROFILE_HIGH, NULL);
    g_object_set(G_OBJECT(encoder), "level", (guint)OMX_H264ENC_LVL_51, NULL);
    g_object_set(G_OBJECT(encoder), "encodingPreset", (guint)OMX_H264ENC_ENC_PRE_HSMQ, NULL);
    g_object_set(G_OBJECT(encoder), "rateControlPreset", (guint)OMX_H264ENC_RATE_LOW_DELAY, NULL);
    g_object_set(G_OBJECT(encoder), "framerate", (guint)LIVE_MAX_FRAMERATE, NULL);

    gst_bin_add_many(GST_BIN(state->pipeline), queue, encoder, neon, tee, NULL);
    gst_element_link_many(queue, encoder, neon, tee, NULL);

    /* Add sinks to consume the live h.264 stream. */
#if ENABLE_RTSP_SERVER
    sinkpad = cam_rtsp_sink(state);
    if (sinkpad) {
        GstPad *teepad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(teepad, sinkpad);
        gst_object_unref(sinkpad);
    }
#endif

    sinkpad = cam_liverec_sink(state, args);
    if (sinkpad) {
        GstPad *teepad = gst_element_get_request_pad(tee, "src%d");
        gst_pad_link(teepad, sinkpad);
        gst_object_unref(sinkpad);
    }

    return gst_element_get_static_pad(queue, "sink");
}
