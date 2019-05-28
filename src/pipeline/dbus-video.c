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
#include <dbus/dbus-glib.h>

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
    cam_dbus_dict_add_boolean(dict, "playback", (state->playstate == PLAYBACK_STATE_PLAY));
    cam_dbus_dict_add_boolean(dict, "filesave", (state->playstate == PLAYBACK_STATE_FILESAVE));
    cam_dbus_dict_add_uint(dict, "totalFrames", state->seglist.totalframes);
    cam_dbus_dict_add_uint(dict, "totalSegments", state->seglist.totalsegs);
    cam_dbus_dict_add_int(dict, "position", state->position);
    cam_dbus_dict_add_uint(dict, "segment", 0);
    if (state->playstate == PLAYBACK_STATE_FILESAVE) {
        double estrate = (FRAMERATE_IVAL_BUCKETS * 1000000) / (double)state->frameisum;
        cam_dbus_dict_add_float(dict, "framerate", estrate);
        cam_dbus_dict_add_string(dict, "filename", state->args.filename);
    } else {
        cam_dbus_dict_add_float(dict, "framerate", (double)state->playrate);
    }
    return dict;
}

static uint16_t
cam_dbus_parse_focus_peak(GHashTable *dict, const char *name, uint16_t defval)
{
    gpointer x = g_hash_table_lookup(dict, name);

    /* If we got a boolean, then select Cyan as the default color. */
    if (x && G_VALUE_HOLDS_BOOLEAN(x)) {
        return g_value_get_boolean(x) ? DISPLAY_CTL_FOCUS_PEAK_CYAN : 0;
    }
    /* Check for a string naming the color of choice. */
    if (x && G_VALUE_HOLDS_STRING(x)) {
        const char *color = g_value_get_string(x);
        switch (tolower(color[0])) {
            case 'r': return DISPLAY_CTL_FOCUS_PEAK_RED;
            case 'g': return DISPLAY_CTL_FOCUS_PEAK_GREEN;
            case 'b': return DISPLAY_CTL_FOCUS_PEAK_BLUE;
            case 'c': return DISPLAY_CTL_FOCUS_PEAK_CYAN;
            case 'm': return DISPLAY_CTL_FOCUS_PEAK_MAGENTA;
            case 'y': return DISPLAY_CTL_FOCUS_PEAK_YELLOW;
            case 'w': return DISPLAY_CTL_FOCUS_PEAK_WHITE;
            default:  return 0;
        }
    }
    /* Although not recommended, also accept an integer. */
    if (x && G_VALUE_HOLDS_UINT(x)) return (g_value_get_uint(x) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
    if (x && G_VALUE_HOLDS_ULONG(x)) return (g_value_get_ulong(x) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
    if (x && G_VALUE_HOLDS_INT(x)) return (g_value_get_int(x) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
    if (x && G_VALUE_HOLDS_LONG(x)) return (g_value_get_long(x) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
    /* Otherwise, return the default value. */
    return defval;
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
    GObjectClass parent;
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

    /* Return the dict of set values. */
    *data = cam_dbus_video_get(state, names);
    if (errdict) {
        cam_dbus_dict_take_boxed(*data, "error", CAM_DBUS_HASH_MAP, errdict);
    } else {
        cam_dbus_dict_free(errdict);
    }
    
    /* Generate an update signal for any parameters that were set. */
    if (names) {
        if (i) dbus_signal_update(state, names);
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

    if (loopcount) {
        playback_loop(state, position, framerate, loopcount);
    } else {
        playback_play(state, position, framerate);
    }
    state->args.mode = PIPELINE_MODE_PLAY;
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
    gboolean zebra_en = cam_dbus_dict_get_boolean(args, "zebra", state->config.zebra_level > 0.0);
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
    state->config.zebra_level = zebra_en ? 0.05 : 0.0;
    state->config.peak_color = cam_dbus_parse_focus_peak(args, "peaking", state->config.peak_color);
    if (state->config.zebra_level > 0.0) {
        state->fpga->zebra->threshold = 255.0 * (1 - state->config.zebra_level);
        state->control |= DISPLAY_CTL_ZEBRA_ENABLE;
    } else {
        state->control &= ~DISPLAY_CTL_ZEBRA_ENABLE;
    }
    if (state->config.peak_color) {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_COLOR;
        state->control |= (DISPLAY_CTL_FOCUS_PEAK_ENABLE | state->config.peak_color);
    } else {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_ENABLE;
    }
    if (state->source.color) {
        state->control |= DISPLAY_CTL_COLOR_MODE;
    } else {
        state->control &= ~DISPLAY_CTL_COLOR_MODE;
    }

    /* Apply Changes. */
    if (state->playstate == PLAYBACK_STATE_PLAY) {
        uint32_t dcontrol;
        if (diff) cam_lcd_reconfig(state, &state->config);

        dcontrol = state->fpga->display->control;
        dcontrol &= ~(DISPLAY_CTL_ZEBRA_ENABLE | DISPLAY_CTL_COLOR_MODE);
        dcontrol &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_FOCUS_PEAK_COLOR);
        state->fpga->display->control = dcontrol | state->control;
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
    gboolean zebra_en = cam_dbus_dict_get_boolean(args, "zebra", state->config.zebra_level > 0.0);

    /* Update the live display flags. */
    state->args.mode = PIPELINE_MODE_PLAY;
    state->source.color = cam_dbus_dict_get_boolean(args, "color", state->source.color);
    state->config.zebra_level = zebra_en ? 0.05 : 0.0;
    state->config.peak_color = cam_dbus_parse_focus_peak(args, "peaking", state->config.peak_color);
    if (state->config.zebra_level > 0.0) {
        state->fpga->zebra->threshold = 255.0 * (1 - state->config.zebra_level);
        state->control |= DISPLAY_CTL_ZEBRA_ENABLE;
    } else {
        state->control &= ~DISPLAY_CTL_ZEBRA_ENABLE;
    }
    if (state->config.peak_color) {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_COLOR;
        state->control |= (DISPLAY_CTL_FOCUS_PEAK_ENABLE | state->config.peak_color);
    } else {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_ENABLE;
    }
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

    /* If not in playback mode, a restart is required. */
    state->args.mode = PIPELINE_MODE_LIVE;
    if ((state->playstate != PLAYBACK_STATE_PLAY) && (state->playstate != PLAYBACK_STATE_LIVE)) {
        cam_pipeline_restart(state);
    }
    /* If cropping geometry has changed, a restart is required. */
    else if (diff) {
        cam_pipeline_restart(state);
    }
    /* Otherwise, go directly to live. */
    else {
        playback_live(state);
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
    if (state->playstate == PLAYBACK_STATE_FILESAVE) {
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

void
dbus_service_launch(struct pipeline_state *state)
{
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;
    GError* error = NULL;
    guint result;

    /* Init glib */
    g_type_init();
    state->video = g_object_new(CAM_VIDEO_TYPE, NULL);
    state->video->state = state;

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
    dbus_g_connection_register_g_object(bus, CAM_DBUS_VIDEO_PATH, G_OBJECT(state->video));
    printf("Registered video control device at %s\n", CAM_DBUS_VIDEO_PATH);
}

void
dbus_signal_sof(struct pipeline_state *state)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(state->video);
    g_signal_emit(state->video, vclass->sof_signalid, 0, cam_dbus_video_status(state));
}

void
dbus_signal_eof(struct pipeline_state *state, const char *err)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(state->video);
    GHashTable *status = cam_dbus_video_status(state);
    if (err && strlen(err)) {
        cam_dbus_dict_add_string(status, "error", err);
    }
    g_signal_emit(state->video, vclass->eof_signalid, 0, status);
}

void
dbus_signal_segment(struct pipeline_state *state)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(state->video);
    g_signal_emit(state->video, vclass->seg_signalid, 0, cam_dbus_video_status(state));
}

void
dbus_signal_update(struct pipeline_state *state, const char **names)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(state->video);
    g_signal_emit(state->video, vclass->update_signalid, 0, cam_dbus_video_get(state, names));
}
