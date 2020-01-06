/****************************************************************************
 *  Copyright (C) 2017-2018 Kron Technologies Inc <http://www.krontech.ca>. *
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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "pipeline.h"
#include "api/cam-rpc.h"

static gboolean
parse_resolution(const char *resxy, unsigned int *x, unsigned int *y)
{
    char *e;
    *x = strtoul(resxy, &e, 10);
    if (*e != 'x') return FALSE;
    *y = strtoul(e+1, &e, 10);
    if (*e != '\0') return FALSE;
    return TRUE;
}

static GHashTable *
cam_dbus_video_status(struct pipeline_state *state)
{
    GHashTable *dict = cam_dbus_dict_new();
    if (!dict) return NULL;

    cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
    cam_dbus_dict_add_boolean(dict, "playback", (state->runmode == PIPELINE_MODE_PLAY));
    cam_dbus_dict_add_boolean(dict, "filesave", PIPELINE_IS_SAVING(state->runmode));
    cam_dbus_dict_add_uint(dict, "totalFrames", state->seglist.totalframes);
    cam_dbus_dict_add_uint(dict, "totalSegments", state->seglist.totalsegs);
    cam_dbus_dict_add_int(dict, "position", state->position);
    cam_dbus_dict_add_uint(dict, "segment", 0);
    if (PIPELINE_IS_SAVING(state->runmode)) {
        double estrate = (FRAMERATE_IVAL_BUCKETS * 1000000) / (double)state->frameisum;
        cam_dbus_dict_add_float(dict, "framerate", estrate);
        cam_dbus_dict_add_string(dict, "filename", state->args.filename);
    } else {
        cam_dbus_dict_add_float(dict, "framerate", (double)state->playrate);
    }
    return dict;
}

static GHashTable *
cam_dbus_video_get(struct pipeline_state *state, const char **names)
{
    int i;
    int errcount = 0;
    GHashTable *dict = cam_dbus_dict_new();
    GHashTable *errdict = cam_dbus_dict_new();
    if (!dict) return NULL;

    for (i = 0; names[i] != NULL; i++) {
        if (!dbus_get_param(state, names[i], dict)) {
            cam_dbus_dict_add_string(errdict, names[i], "Unknown parameter");
            errcount++;
        }
    }

    /* Include an error dictionary for anything that couldn't be found. */
    if (errcount) {
        cam_dbus_dict_take_boxed(dict, "error", CAM_DBUS_HASH_MAP, errdict);
    } else {
        cam_dbus_dict_free(errdict);
    }
    
    return dict;
}

/*-------------------------------------
 * DBUS Video Control API
 *-------------------------------------
 */
typedef struct CamVideo {
    GObjectClass    parent;
    guint           timeout_id;
    struct pipeline_state *state;
} CamVideo;

typedef struct CamVideoClass {
    GObjectClass parent;
    guint sof_signalid;
    guint eof_signalid;
    guint seg_signalid;
    guint update_signalid;
} CamVideoClass;

static gboolean
cam_video_status(CamVideo *vobj, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    *data = cam_dbus_video_status(state);
    return (*data != NULL);
}

static gboolean
cam_video_get(CamVideo *vobj, const char **names, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    *data = cam_dbus_video_get(state, names);
    return (*data != NULL);
}

/* Callback helper to flush parameter changes. */
static gboolean
dbus_flush_params(gpointer data)
{
    CamVideo *vobj = data;
    struct pipeline_state *state = vobj->state;

    /* Write the configuration file if its dirty */
    if (state->config.filename && state->config.dirty) {
        FILE *fp = fopen(state->config.filename, "w");
        if (fp) {
            dbus_save_params(state, fp);
            fclose(fp);
        }
        state->config.dirty = FALSE;
    }

    /* The timeout source should get removed after this */
    vobj->timeout_id = 0;
    return FALSE;
}

static gboolean
cam_video_set(CamVideo *vobj, GHashTable *args, GHashTable **data, GError *error)
{
    struct pipeline_state *state = vobj->state;
    const char **names = (const char **)g_new0(char *, g_hash_table_size(args) + 1);
    int i = 0;

    GHashTable *errdict = cam_dbus_dict_new();
    char err[PIPELINE_ERROR_MAXLEN];
    int errcount = 0;

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, args);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (dbus_set_param(state, key, value, err)) {
            if (names) names[i++] = key;
        } else {
            err[sizeof(err)-1] = '\0';
            cam_dbus_dict_add_string(errdict, key, err);
            errcount++;
        }
    }

    /* If the configuration is now dirty, start a timer to flush it to disk. */
    if (state->config.dirty) {
        if (vobj->timeout_id) g_source_remove(vobj->timeout_id);
        vobj->timeout_id = g_timeout_add(1000, dbus_flush_params, vobj);
    }

    /* Return the dict of set values. */
    *data = cam_dbus_video_get(state, names);
    if (errdict) {
        cam_dbus_dict_take_boxed(*data, "error", CAM_DBUS_HASH_MAP, errdict);
    } else {
        cam_dbus_dict_free(errdict);
    }
    
    /* Generate an update signal for any parameters that were set. */
    if (names) {
        if (i) dbus_signal_update(vobj, names);
        g_free(names);
    }
    return (data != NULL);
}

static gboolean
cam_video_describe(CamVideo *vobj, GHashTable **data, GError *error)
{
    *data = dbus_describe_params(vobj->state);
    return (*data != NULL);
}

static gboolean
cam_video_flush(CamVideo *vobj, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    playback_flush(state);
    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean
cam_video_playback(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    unsigned long position = cam_dbus_dict_get_uint(args, "position", state->position);
    unsigned long loopcount = cam_dbus_dict_get_uint(args, "loopcount", 0);
    int framerate = cam_dbus_dict_get_int(args, "framerate", state->playrate);

    state->args.mode = PIPELINE_MODE_PLAY;

    /* If we're in playback or live, send a seek command. */
    if ((state->playstate == PLAYBACK_STATE_LIVE) || (state->playstate == PLAYBACK_STATE_PLAY)) {
        if (loopcount) {
            playback_loop(state, position, framerate, loopcount);
        } else {
            playback_play(state, position, framerate);
        }
    }
    /* Otherwise, unless we're saving, give the video system a reboot. */
    else if (!PIPELINE_IS_SAVING(state->runmode)) {
        /* Setup the playback parameters. */
        state->playrate = framerate;
        state->playstart = position;
        state->playloop = (loopcount != 0);
        state->playlength = (loopcount == 0) ? state->seglist.totalframes : loopcount;
        cam_pipeline_restart(state);
    }

    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean
cam_video_configure(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    unsigned long hres = cam_dbus_dict_get_uint(args, "hres", state->config.hres);
    unsigned long vres = cam_dbus_dict_get_uint(args, "vres", state->config.vres);
    unsigned long xoff = cam_dbus_dict_get_uint(args, "xoff", state->config.xoff);
    unsigned long yoff = cam_dbus_dict_get_uint(args, "yoff", state->config.yoff);
    unsigned long diff = 0;

    /* Sanity-check the new display configuration. */
    if (((hres + xoff) > CAM_LCD_HRES) || ((vres + yoff) > CAM_LCD_VRES)) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid display resolution and offset");
        return 0;
    }

    /* Update the display resolution */
    diff |= (state->config.hres ^ hres) | (state->config.vres ^ vres);
    diff |= (state->config.xoff ^ xoff) | (state->config.yoff ^ yoff);
    state->config.hres = hres;
    state->config.vres = vres;
    state->config.xoff = xoff;
    state->config.yoff = yoff;

    /* Update the live display flags. */
    state->source.color = cam_dbus_dict_get_boolean(args, "color", state->source.color);
    if (state->source.color) {
        state->control |= DISPLAY_CTL_COLOR_MODE;
    } else {
        state->control &= ~DISPLAY_CTL_COLOR_MODE;
    }

    /* Apply all changes immediately when live. */
    if (state->playstate == PLAYBACK_STATE_LIVE) {
        uint32_t dcontrol;
        if (diff) cam_lcd_reconfig(state, &state->config);

        /* Possibly update the color/mono control bit. */
        dcontrol = state->fpga->display->control;
        dcontrol &= ~DISPLAY_CTL_COLOR_MODE;
        dcontrol |= (state->control & DISPLAY_CTL_COLOR_MODE);
        state->fpga->display->control = dcontrol;
    }
    /* Apply geometry changes immediately when in playback. */
    else if ((state->playstate == PLAYBACK_STATE_PLAY) && (diff)) {
        cam_lcd_reconfig(state, &state->config);
    }

    *data = cam_dbus_video_status(state);
    return (data != NULL);
}


static gboolean
cam_video_livedisplay(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    unsigned long diff = 0;
    unsigned int  flip = cam_dbus_dict_get_boolean(args, "flip", state->source.flip);
    unsigned long cropx = cam_dbus_dict_get_uint(args, "cropx", 0);
    unsigned long cropy = cam_dbus_dict_get_uint(args, "cropy", 0);
    unsigned long startx = cam_dbus_dict_get_uint(args, "startx", 0);
    unsigned long starty = cam_dbus_dict_get_uint(args, "starty", 0);
    
    /* Update the live display flags. */
    state->source.color = cam_dbus_dict_get_boolean(args, "color", state->source.color);
    if (state->source.color) {
        state->control |= DISPLAY_CTL_COLOR_MODE;
    } else {
        state->control &= ~DISPLAY_CTL_COLOR_MODE;
    }

    /* Check if resolution has changed. */
    diff |= (flip ^ state->source.flip);
    diff |= (cropx ^ state->source.cropx);
    diff |= (cropy ^ state->source.cropy);
    diff |= (startx ^ state->source.startx);
    diff |= (starty ^ state->source.starty);
    state->source.flip = flip;
    state->source.cropx = cropx;
    state->source.cropy = cropy;
    state->source.startx = startx;
    state->source.starty = starty;

    /* If we're in playback or live, send a seek command. */
    state->args.mode = PLAYBACK_STATE_LIVE;
    if ((state->playstate == PLAYBACK_STATE_LIVE) || (state->playstate == PLAYBACK_STATE_PLAY)) {
        if (diff) {
            /* A change of video geometry will require a restart. */
            cam_pipeline_restart(state);
        } else {
            /* Otherwise, seek directly to live. */
            playback_live(state);
        }
    }
    /* Otherwise, unless we're saving, give the video system a reboot. */
    else if (!PIPELINE_IS_SAVING(state->runmode)) {
        cam_pipeline_restart(state);
    }

    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean
cam_video_recordfile(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    GHashTable *dict;
    const char *filename = cam_dbus_dict_get_string(args, "filename", NULL);
    const char *format = cam_dbus_dict_get_string(args, "format", NULL);

    /* Format and filename are mandatory */
    if (!filename || !format) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Missing arguments");
        return 0;
    }

    /* Sanity check the file name. */
    if (filename[0] != '/') {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid filename");
        return 0;
    }
    if (strlen(filename) >= sizeof(state->args.filename)) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "File name too long");
        return 0;
    }
    strcpy(state->args.filename, filename);

    /* Make sure that an encoding operation is not already in progress. */
    if (PIPELINE_IS_SAVING(state->runmode)) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Encoding in progress");
        return 0;
    }

    /* TODO: Perform a free-space check if the file does not already exists. */
    /* TODO: Test that the file and/or directory is writeable. */
    /* TODO: Test that the destination file is *NOT* the root filesystem. */

    /* Dive deeper based on the format */
    state->args.start = cam_dbus_dict_get_uint(args, "start", 0);
    state->args.length = cam_dbus_dict_get_uint(args, "length", state->seglist.totalframes);
    
    /* Accept all microsoft variants of the H264 FOURCC codes */
    if ((strcasecmp(format, "h264") == 0) || (strcasecmp(format, "x264") == 0)) {
        state->args.mode = PIPELINE_MODE_H264;
        state->args.framerate = cam_dbus_dict_get_uint(args, "framerate", 30);
        state->args.bitrate = cam_dbus_dict_get_uint(args, "bitrate", 40000000);
    }
    else if (strcasecmp(format, "dng") == 0) {
        state->args.mode = PIPELINE_MODE_DNG;
    }
    else if (strcasecmp(format, "tiff") == 0) {
        /* Processed RGB or monochrome TIFF files. */
        state->args.mode = PIPELINE_MODE_TIFF;
    }
    else if (strcasecmp(format, "tiffraw") == 0) {
        /* Unprocessed monochrome TIFF files. */
        state->args.mode = PIPELINE_MODE_TIFF_RAW;
    }
    /* Handle Bayer and monochrome formats interchangeably */
    else if ((strcasecmp(format, "byr2") == 0) || 
                (strcasecmp(format, "gb16") == 0) ||
                (strcasecmp(format, "gr16") == 0) ||
                (strcasecmp(format, "rg16") == 0) ||
                (strcasecmp(format, "y16") == 0)) {
        /* 12-bit samples padded with lsb zeros to fit 16-bit data. */
        state->args.mode = PIPELINE_MODE_RAW16;
    }
    else if ((strcasecmp(format, "pRAA") == 0) || 
                (strcasecmp(format, "pgAA") == 0) ||
                (strcasecmp(format, "pGAA") == 0) ||
                (strcasecmp(format, "pBAA") == 0) ||
                (strcasecmp(format, "y12b") == 0)) {
        /* 12-bit samples packed (2 pixels stored in 3 bytes) */
        state->args.mode = PIPELINE_MODE_RAW12;
    }
    /* Otherwise, this encoding format is not supported. */
    else {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Unsupported encoding method");
        return 0;
    }

    /* Restart the video pipeline to enter recording mode. */
    cam_pipeline_restart(state);
    *data = cam_dbus_video_status(state);
    return 1;
}

static gboolean
cam_video_pause(CamVideo *vobj, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    state->args.mode = PIPELINE_MODE_PAUSE;
    cam_pipeline_restart(state);
    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean
cam_video_stop(CamVideo *vobj, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    cam_pipeline_restart(state);
    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean
cam_video_overlay(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    const char *format = cam_dbus_dict_get_string(args, "format", "");
    const char *position = cam_dbus_dict_get_string(args, "position", "top");
    const char *size = cam_dbus_dict_get_string(args, "textbox", "0x0");

    /* Parse the position of the textbox. */
    if (strcasecmp(position, "top") == 0) {
        state->overlay.xoff = 0;
        state->overlay.yoff = 0;
    }
    else if (strcasecmp(position, "bottom") == 0) {
        state->overlay.xoff = 0;
        state->overlay.yoff = UINT_MAX;
    }
    else if (!parse_resolution(position, &state->overlay.xoff, &state->overlay.yoff)) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid overlay offset");
        return 0;
    }

    /* Format string. */
    if (strlen(format) >= sizeof(state->overlay.format)) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Overlay format string too long");
        return 0;
    }
    state->overlay.enable = (*format != '\0');
    strncpy(state->overlay.format, format, sizeof(state->overlay.format));

    /* Textbox size, or 0x0 for the full width. */
    state->overlay.width = 0;
    state->overlay.height = 0;
    if (!parse_resolution(size, &state->overlay.width, &state->overlay.height)) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid overlay size");
        return 0;
    }

    /* Update the overlay configuration in playback modes. */
    if (state->playstate == PLAYBACK_STATE_PLAY) {
        overlay_setup(state);
    }

    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

#include "api/cam-dbus-video.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */

GType cam_video_get_type(void);

#define CAM_VIDEO_TYPE              (cam_video_get_type())
#define CAM_VIDEO_GET_CLASS(_vobj_) (G_TYPE_INSTANCE_GET_CLASS((_vobj_), CAM_VIDEO_TYPE, CamVideoClass))

static void
cam_video_init(CamVideo *vobj)
{
    g_assert(vobj != NULL);
    vobj->timeout_id = 0;
    vobj->state = NULL;
}

static void
cam_video_class_init(CamVideoClass *vclass)
{
    g_assert(vclass != NULL);

    /* Register signals. */
    vclass->sof_signalid = g_signal_new("sof", G_OBJECT_CLASS_TYPE(vclass),
                    G_SIGNAL_RUN_LAST,
                    0,
                    NULL, NULL,             /* GSignalAccumulator and its user data. */
                    NULL,                   /* C signal marshaller - should be replaced with static version. */
                    G_TYPE_NONE,            /* GType of the return value */
                    1, CAM_DBUS_HASH_MAP);  /* Number of parameters and their signatures. */

    vclass->eof_signalid = g_signal_new("eof", G_OBJECT_CLASS_TYPE(vclass),
                    G_SIGNAL_RUN_LAST,   /* How and when to run the signal. */
                    0,
                    NULL, NULL,             /* GSignalAccumulator and its user data. */
                    NULL,                   /* C signal marshaller - should be replaced with static version. */
                    G_TYPE_NONE,            /* Return GType of the return value. */
                    1, CAM_DBUS_HASH_MAP);  /* Number of parameters and their signatures. */

    vclass->seg_signalid = g_signal_new("segment", G_OBJECT_CLASS_TYPE(vclass),
                    G_SIGNAL_RUN_LAST,   /* How and when to run the signal. */
                    0,
                    NULL, NULL,             /* GSignalAccumulator and its user data. */
                    NULL,                   /* C signal marshaller - should be replaced with static version. */
                    G_TYPE_NONE,            /* Return GType of the return value. */
                    1, CAM_DBUS_HASH_MAP);  /* Number of parameters and their signatures. */

    vclass->update_signalid = g_signal_new("update", G_OBJECT_CLASS_TYPE(vclass),
                    G_SIGNAL_RUN_LAST,   /* How and when to run the signal. */
                    0,
                    NULL, NULL,             /* GSignalAccumulator and its user data. */
                    NULL,                   /* C signal marshaller - should be replaced with static version. */
                    G_TYPE_NONE,            /* Return GType of the return value. */
                    1, CAM_DBUS_HASH_MAP);  /* Number of parameters and their signatures. */

    dbus_g_object_type_install_info(CAM_VIDEO_TYPE, &dbus_glib_cam_video_object_info);
}

G_DEFINE_TYPE(CamVideo, cam_video, G_TYPE_OBJECT)

struct CamVideo *
dbus_service_launch(struct pipeline_state *state)
{
    struct CamVideo *video;
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;
    GError* error = NULL;
    guint result;

    /* Init glib and start the D-Bus worker thread. */
    g_type_init();
    video = g_object_new(CAM_VIDEO_TYPE, NULL);
    video->state = state;

    /* Bring up DBus and register with the system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }

    proxy = dbus_g_proxy_new_for_name(bus, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);
    if (proxy == NULL) {
        g_printerr("Failed to get a proxy for D-Bus (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (!dbus_g_proxy_call(proxy,
                            "RequestName",
                            &error,
                            G_TYPE_STRING, CAM_DBUS_VIDEO_SERVICE,
                            G_TYPE_UINT, 0,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &result,
                            G_TYPE_INVALID)) {
        g_printerr("D-Bus.RequstName RPC failed (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (result != 1) {
        g_printerr("D-Bus.RequstName call failed for %s\n", CAM_DBUS_VIDEO_SERVICE);
        exit(EXIT_FAILURE);
    }
    dbus_g_connection_register_g_object(bus, CAM_DBUS_VIDEO_PATH, G_OBJECT(video));
    printf("Registered video control device at %s\n", CAM_DBUS_VIDEO_PATH);

    return video;
}

void
dbus_service_cleanup(struct CamVideo *vobj)
{
    if (vobj->timeout_id) g_source_remove(vobj->timeout_id);
    dbus_flush_params(vobj);
}

struct dbus_signal_data {
    CamVideo    *video;
    guint       signal_id;
    GQuark      detail;
    GHashTable  *payload;
};

static gboolean
dbus_defer_emit(gpointer gdata)
{
    struct dbus_signal_data *data = gdata;
    g_signal_emit(data->video, data->signal_id, data->detail, data->payload);
    g_free(data);
    return FALSE;
}

/*
 * D-Bus signals are not multi-thread safe, so we must first take care to pass
 * them to the GLib main context before emitting the signal. This helper function
 * takes the signal parameters, bundles them together into a structure and
 * schedules the signal to be generated from a GLib idle handler.
 */
static void
dbus_defer_signal(CamVideo *video, guint signal_id, GQuark detail, GHashTable *payload)
{
    GSource *source;
    struct dbus_signal_data *data;
    
    /* Bundle together the deferred signal data */
    data = g_malloc(sizeof(struct dbus_signal_data));
    if (!data) {
        cam_dbus_dict_free(payload);
        return;
    }
    data->video = video;
    data->signal_id = signal_id;
    data->detail = detail;
    data->payload = payload;

    /* Create an idle source to emit the signal data. */
    if (g_idle_add(dbus_defer_emit, data) == 0) {
        cam_dbus_dict_free(payload);
        g_free(data);
    }
}

void
dbus_signal_sof(CamVideo *video)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(video);
    dbus_defer_signal(video, vclass->sof_signalid, 0, cam_dbus_video_status(video->state));
}

void
dbus_signal_eof(CamVideo *video, const char *err)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(video);
    GHashTable *status = cam_dbus_video_status(video->state);
    if (err && strlen(err)) {
        cam_dbus_dict_add_string(status, "error", err);
    }
    dbus_defer_signal(video, vclass->eof_signalid, 0, status);
}

void
dbus_signal_segment(CamVideo *video)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(video);
    dbus_defer_signal(video, vclass->seg_signalid, 0, cam_dbus_video_status(video->state));
}

void
dbus_signal_update(CamVideo *video, const char **names)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(video);
    dbus_defer_signal(video, vclass->update_signalid, 0, cam_dbus_video_get(video->state, names));
}
