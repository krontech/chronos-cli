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

#include "pipeline.h"
#include "utils.h"
#include "api/cam-rpc.h"

#define PARAM_F_NOTIFY   0x0001
#define PARAM_F_SAVE     0x0002


struct pipeline_param {
    const char      *name;
    GType           type;
    unsigned long   flags;
    /* All parameters can be read using the type and possibly extra data. */
    size_t          offset;
    const void      *extra;
    /* Complex parameters - callbacks to do translation and stateful stuff. */
    gboolean        (*setter)(struct pipeline_state *state, const struct pipeline_param *param, GValue *val, char *err);
};
static const struct pipeline_param *cam_dbus_params[];

/* Getter method for complex/boxed types. */
typedef GValue *(*param_boxed_getter_t)(struct pipeline_state *, const struct pipeline_param *);

/* Retrieive the parameter value and add it to the hash table */
static gboolean
dbus_get_value(struct pipeline_state *state, const struct pipeline_param *p, GHashTable *data)
{
    void *pvalue = ((unsigned char *)state) + p->offset;
    switch (p->type) {
        case G_TYPE_BOOLEAN:
            cam_dbus_dict_add_boolean(data, p->name, *(gboolean *)pvalue);
            return TRUE;
        case G_TYPE_LONG:
            cam_dbus_dict_add_int(data, p->name, *(long *)pvalue);
            return TRUE;
        case G_TYPE_ULONG:
            cam_dbus_dict_add_uint(data, p->name, *(unsigned long *)pvalue);
            return TRUE;
        case G_TYPE_DOUBLE:
            cam_dbus_dict_add_float(data, p->name, *(double *)pvalue);
            return TRUE;
        case G_TYPE_STRING:
            cam_dbus_dict_add_string(data, p->name, pvalue);
            return TRUE;
        case G_TYPE_ENUM:
            cam_dbus_dict_add_string(data, p->name, enumval_name(p->extra, *(int *)pvalue, "undefined"));
            return TRUE;
        case G_TYPE_BOXED:{
            param_boxed_getter_t getter = p->extra;
            GValue *gval = getter(state, p);
            if (gval) cam_dbus_dict_add(data, p->name, gval);
            return (gval != NULL);
        }
        default:
            /* Unsupported type */
            return FALSE;
    }
}

/*
 * Minimal setter implementation - converts the GValue into its underlying
 * type and copy it into the state structure with no value checking.
 */
static gboolean
cam_generic_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    void *pvalue = ((unsigned char *)state) + p->offset;
    switch (p->type) {
        case G_TYPE_BOOLEAN:
            *(gboolean *)pvalue = g_value_get_boolean(val);
            return TRUE;
        case G_TYPE_LONG:
            *(long *)pvalue = g_value_get_long(val);
            return TRUE;
        case G_TYPE_ULONG:
            *(unsigned long *)pvalue = g_value_get_ulong(val);
            return TRUE;
        case G_TYPE_DOUBLE:
            *(double *)pvalue = g_value_get_double(val);
            return TRUE;
        case G_TYPE_STRING:
        case G_TYPE_ENUM:
        case G_TYPE_BOXED:
        default:
            /* Unsupported types */
            return FALSE;
    }
}

struct enumval playback_states[] = {
    {PLAYBACK_STATE_PAUSE,      "paused"},
    {PLAYBACK_STATE_LIVE,       "live"},
    {PLAYBACK_STATE_PLAY,       "play"},
    {PLAYBACK_STATE_FILESAVE,   "filesave"},
    { 0, NULL }
};

struct enumval focus_peak_colors[] = {
    { 0,                            "black"},
    {DISPLAY_CTL_FOCUS_PEAK_RED,    "red"},
    {DISPLAY_CTL_FOCUS_PEAK_GREEN,  "green"},
    {DISPLAY_CTL_FOCUS_PEAK_BLUE,   "blue"},
    {DISPLAY_CTL_FOCUS_PEAK_CYAN,   "cyan"},
    {DISPLAY_CTL_FOCUS_PEAK_MAGENTA, "magenta"},
    {DISPLAY_CTL_FOCUS_PEAK_YELLOW, "yellow"},
    {DISPLAY_CTL_FOCUS_PEAK_WHITE,  "white"},
    { 0, NULL }
};

static gboolean
cam_focus_peak_color_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->config.peak_color = g_value_get_int(val);
    state->control &= ~DISPLAY_CTL_FOCUS_PEAK_COLOR;
    state->control |= (state->config.peak_color);

    /* Update the FPGA if we're in live mode. */
    if (state->playstate == PLAYBACK_STATE_LIVE) {
        uint32_t dcontrol = state->fpga->display->control;
        dcontrol &= ~(DISPLAY_CTL_ZEBRA_ENABLE | DISPLAY_CTL_COLOR_MODE);
        dcontrol &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_FOCUS_PEAK_COLOR);
        state->fpga->display->control = dcontrol | state->control;
    }
    return TRUE;
}
static const struct pipeline_param cam_focus_peak_color_param = {
    .name = "focusPeakingColor",
    .type = G_TYPE_ENUM,
    .flags = PARAM_F_NOTIFY | PARAM_F_SAVE,
    .offset = offsetof(struct pipeline_state, config.peak_color),
    .extra = focus_peak_colors,
    .setter = cam_focus_peak_color_setter,
};

static gboolean
cam_focus_peak_level_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    /* Parse and set the focus peaking level. */
    double fplevel = g_value_get_double(val);
    if (fplevel < 0.0) fplevel = 0.0;
    if (fplevel > 1.0) fplevel = 1.0;
    state->config.peak_level = fplevel;
    if (state->config.peak_level > 0.0) {
        state->control |= DISPLAY_CTL_FOCUS_PEAK_ENABLE;
    } else {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_ENABLE;
    }

    /* Update the FPGA directly if already in live mode. */
    if (state->playstate == PLAYBACK_STATE_LIVE) {
        state->fpga->display->peaking_thresh = 35 - (20 * fplevel);
        if (state->config.peak_level > 0.0) {
            state->fpga->display->control |= DISPLAY_CTL_FOCUS_PEAK_ENABLE;
        } else {
            state->fpga->display->control &= ~DISPLAY_CTL_FOCUS_PEAK_ENABLE;
        }
    }
    return TRUE;
}
static const struct pipeline_param cam_focus_peak_level_param = {
    .name = "focusPeakingLevel",
    .type = G_TYPE_DOUBLE,
    .flags = PARAM_F_NOTIFY | PARAM_F_SAVE,
    .offset = offsetof(struct pipeline_state, config.peak_level),
    .setter = cam_focus_peak_level_setter,
};

static gboolean
cam_zebra_level_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    /* Parse and set the zebra level. */
    double zlevel = g_value_get_double(val);
    if (zlevel < 0.0) zlevel = 0.0;
    if (zlevel > 1.0) zlevel = 1.0;
    state->config.zebra_level = zlevel;
    if (state->config.zebra_level > 0.0) {
        state->control |= DISPLAY_CTL_ZEBRA_ENABLE;
    } else {
        state->control &= ~DISPLAY_CTL_ZEBRA_ENABLE;
    }

    /* Update the FPGA directly if already in live mode. */
    if (state->playstate == PLAYBACK_STATE_LIVE) {
        if (state->config.zebra_level > 0.0) {
            state->fpga->display->control |= DISPLAY_CTL_ZEBRA_ENABLE;
            state->fpga->zebra->threshold = 255.0 * (1 - state->config.zebra_level);
        } else {
            state->fpga->display->control &= ~DISPLAY_CTL_ZEBRA_ENABLE;
        }
    }
    return TRUE;
}
static const struct pipeline_param cam_focus_zebra_level_param = {
    .name = "zebraLevel",
    .type = G_TYPE_DOUBLE,
    .flags = PARAM_F_NOTIFY | PARAM_F_SAVE,
    .offset = offsetof(struct pipeline_state, config.zebra_level),
    .setter = cam_zebra_level_setter,
};

static gboolean
cam_video_zoom_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    double zoom = g_value_get_double(val);
    /* TODO: What limits should actually apply here? */
    if (zoom < 0.25) zoom = 0.25;
    if (zoom > 8.0) zoom = 8.0;
    state->config.video_zoom = zoom;

    /* Perform LCD reconfiguration. */
    if ((state->playstate == PLAYBACK_STATE_LIVE) || (state->playstate == PLAYBACK_STATE_PLAY)) {
        cam_lcd_reconfig(state, &state->config);
    }
    /* TODO: Changing zoom will require a pipeline reboot to take effect. */

    return TRUE;
}
static const struct pipeline_param cam_video_zoom_param = {
    .name = "videoZoom",
    .type = G_TYPE_DOUBLE,
    .flags = PARAM_F_NOTIFY,
    .offset = offsetof(struct pipeline_state, config.video_zoom),
    .setter = cam_video_zoom_setter,
};

static gboolean
cam_overlay_enable_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->overlay.enable = g_value_get_boolean(val);
    if (state->playstate == PLAYBACK_STATE_PLAY) overlay_setup(state);
    return TRUE;
}
static const struct pipeline_param cam_overlay_enable_param = {
    .name = "overlayEnable",
    .type = G_TYPE_BOOLEAN,
    .flags = PARAM_F_NOTIFY | PARAM_F_SAVE,
    .offset = offsetof(struct pipeline_state, overlay.enable),
    .setter = cam_overlay_enable_setter,
};

static gboolean
cam_overlay_format_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    strncpy(state->overlay.format, g_value_get_string(val), sizeof(state->overlay.format));
    state->overlay.format[sizeof(state->overlay.format)-1] = '\0';
    return TRUE;
}
static const struct pipeline_param cam_overlay_format_param = {
    .name = "overlayFormat",
    .type = G_TYPE_STRING,
    .flags = PARAM_F_NOTIFY | PARAM_F_SAVE,
    .offset = offsetof(struct pipeline_state, overlay.format),
    .setter = cam_overlay_format_setter,
};

static const struct pipeline_param cam_playback_position_param = {
    .name = "playbackPosition",
    .type = G_TYPE_LONG,
    .flags = 0,
    .offset = offsetof(struct pipeline_state, position),
    .setter = cam_generic_setter,
};
static const struct pipeline_param cam_playback_rate_param = {
    .name = "playbackRate",
    .type = G_TYPE_LONG,
    .flags = PARAM_F_NOTIFY,
    .offset = offsetof(struct pipeline_state, playrate),
    .setter = cam_generic_setter,
};
static const struct pipeline_param cam_playback_start_param = {
    .name = "playbackStart",
    .type = G_TYPE_ULONG,
    .flags = PARAM_F_NOTIFY,
    .offset = offsetof(struct pipeline_state, playstart),
    .setter = cam_generic_setter,
};
static const struct pipeline_param cam_playback_length_param = {
    .name = "playbackLength",
    .type = G_TYPE_ULONG,
    .flags = PARAM_F_NOTIFY,
    .offset = offsetof(struct pipeline_state, playlength),
    .setter = cam_generic_setter,
};

static GValue *
cam_video_segments_getter(struct pipeline_state *state, const struct pipeline_param *p)
{
    GValue *vboxed;
    GPtrArray *array;
    struct video_segment *seg;
    unsigned long offset = 0;

    pthread_mutex_lock(&state->segmutex);
    array = g_ptr_array_sized_new(state->seglist.totalsegs);
    for (seg = state->seglist.head; seg; seg = seg->next) {
        GHashTable *hash = cam_dbus_dict_new();
        if (hash) {
            cam_dbus_dict_add_uint(hash, "length", seg->nframes);
            cam_dbus_dict_add_uint(hash, "offset", offset);
            cam_dbus_dict_add_uint(hash, "exposure", seg->metadata.exposure);
            cam_dbus_dict_add_uint(hash, "interval", seg->metadata.interval);
            g_ptr_array_add(array, hash);
        }

        offset += seg->nframes;
    }
    pthread_mutex_unlock(&state->segmutex);

    vboxed = g_new0(GValue, 1);
    if (!vboxed) {
        g_ptr_array_set_free_func(array, (GDestroyNotify)g_hash_table_destroy);
        g_ptr_array_free(array, TRUE);
        return NULL;
    }

    g_value_init(vboxed, dbus_g_type_get_collection("GPtrArray", CAM_DBUS_HASH_MAP));
    g_value_take_boxed(vboxed, array);
    return vboxed;
}
static const struct pipeline_param cam_video_segments_param = {
    .name = "videoSegments",
    .type = G_TYPE_BOXED,
    .flags = 0,
    .extra = cam_video_segments_getter,
};

static GValue *
cam_video_config_getter(struct pipeline_state *state, const struct pipeline_param *p)
{
    int i;
    GValue *vboxed;
    GHashTable *hash = cam_dbus_dict_new();

    for (i = 0; cam_dbus_params[i] != NULL; i++) {
        const struct pipeline_param *p = cam_dbus_params[i];
        void *pvalue;
        if (p->flags & PARAM_F_SAVE) {
            dbus_get_value(state, p, hash);
        }
    }
    
    vboxed = g_new0(GValue, 1);
    if (!vboxed) {
        cam_dbus_dict_free(hash);
        return NULL;
    }
    g_value_init(vboxed, CAM_DBUS_HASH_MAP);
    g_value_take_boxed(vboxed, hash);
    return vboxed;
}
static const struct pipeline_param cam_video_config_param = {
    .name = "videoConfig",
    .type = G_TYPE_BOXED,
    .flags = 0,
    .extra = cam_video_config_getter,
};

static const struct pipeline_param cam_video_state_param = {
    .name = "videoState",
    .type = G_TYPE_ENUM,
    .flags = PARAM_F_NOTIFY,
    .offset = offsetof(struct pipeline_state, playstate),
    .extra = playback_states,
};
static const struct pipeline_param cam_video_total_frames_param = {
    .name = "totalFrames",
    .type = G_TYPE_LONG,
    .flags = 0,
    .offset = offsetof(struct pipeline_state, seglist.totalframes),
};
static const struct pipeline_param cam_video_total_segments_param = {
    .name = "totalSegments",
    .type = G_TYPE_LONG,
    .flags = 0,
    .offset = offsetof(struct pipeline_state, seglist.totalsegs),
};

static const struct pipeline_param *cam_dbus_params[] = {
    &cam_video_state_param,
    &cam_video_config_param,
    /* Exposure and focus aids. */
    &cam_focus_peak_color_param,
    &cam_focus_peak_level_param,
    &cam_overlay_enable_param,
    &cam_overlay_format_param,
    &cam_focus_zebra_level_param,
    &cam_video_zoom_param,
    /* Playback position and rate. */
    &cam_playback_position_param,
    &cam_playback_rate_param,
    &cam_playback_start_param,
    &cam_playback_length_param,
    /* Description of recorded video. */
    &cam_video_total_frames_param,
    &cam_video_total_segments_param,
    &cam_video_segments_param,
    /* List termination. */
    NULL
};

gboolean
dbus_get_param(struct pipeline_state *state, const char *name, GHashTable *data)
{
    int i;

    for (i = 0; cam_dbus_params[i] != NULL; i++) {
        const struct pipeline_param *p = cam_dbus_params[i];

        /* Look for a matching parameter by name. */
        if (strcasecmp(p->name, name) == 0) {
            return dbus_get_value(state, p, data);
        }
    }

    /* Otherwise, no such parameter exists with that name. */
    return FALSE;
}

static gboolean
dbus_set_enum(struct pipeline_state *state, const struct pipeline_param *p, GValue *gval, char *err)
{
    const struct enumval *e = p->extra;
    GValue gv_int;

    memset(&gv_int, 0, sizeof(gv_int));
    g_value_init(&gv_int, G_TYPE_INT);

    /* If the value holds a string, search for a match. */
    if (G_VALUE_HOLDS_STRING(gval)) {
        const char *ename;
        for (ename = g_value_get_string(gval); e->name; e++) {
            if (strcasecmp(e->name, ename) == 0) {
                g_value_set_int(&gv_int, e->value);
                return p->setter(state, p, &gv_int, err);
            }
        }
        snprintf(err, PIPELINE_ERROR_MAXLEN, "\'%s\' is not valid for parameter \'%s\'", ename, p->name);
        return FALSE;
    }

    /* Although not recommended, also accept an integer. */
    if (G_VALUE_HOLDS_UINT(gval)) {
        g_value_set_int(&gv_int, g_value_get_uint(gval));
    }
    else if (G_VALUE_HOLDS_ULONG(gval)) {
        g_value_set_int(&gv_int, g_value_get_ulong(gval));
    }
    else if (G_VALUE_HOLDS_INT(gval)) {
        g_value_set_int(&gv_int, g_value_get_int(gval));
    }
    else if (G_VALUE_HOLDS_LONG(gval)) {
        g_value_set_int(&gv_int, g_value_get_long(gval));
    }
    else {
        return FALSE;
    }

    /* Finally, ensure it's a valid enumerated value. */
    while (e->name) {
        if (e->value == g_value_get_int(&gv_int)) {
            return p->setter(state, p, &gv_int, err);
        }
        e++;
    }
    snprintf(err, PIPELINE_ERROR_MAXLEN, "\'%d\' is not valid for parameter \'%s\'", g_value_get_int(&gv_int), p->name);
    return FALSE;
}

gboolean
dbus_set_param(struct pipeline_state *state, const char *name, GValue *gval, char *err)
{
    int i;

    for (i = 0; cam_dbus_params[i] != NULL; i++) {
        const struct pipeline_param *p = cam_dbus_params[i];
        void *pvalue;
        GValue xform;

        /* Look for a matching parameter by name. */
        if (strcasecmp(p->name, name) != 0) continue;
        if (!p->setter) {
            snprintf(err, PIPELINE_ERROR_MAXLEN, "parameter \'%s\' is read only", name);
            return FALSE;
        }

        /* Type transformation if necessary. */
        if (p->type == G_TYPE_ENUM) {
            return dbus_set_enum(state, p, gval, err);
        }
        /* If the value holds the expected type - then set it. */
        else if (G_VALUE_HOLDS(gval, p->type)) {
            return p->setter(state, p, gval, err);
        }

        /* Otherwise, try our best to transform the type and set it. */
        memset(&xform, 0, sizeof(xform));
        g_value_init(&xform, p->type);
        if (g_value_transform(gval, &xform)) {
            return p->setter(state, p, &xform, err);
        }
        /* The user gave us a type we couldn't make sense of. */
        snprintf(err, PIPELINE_ERROR_MAXLEN, "unable to parse parameter \'%s\'", name);
        return FALSE;
    }

    /* Otherwise, no such parameter exists with that name. */
    snprintf(err, PIPELINE_ERROR_MAXLEN, "no such parameter \'%s\' exists", name);
    return FALSE;
}

GHashTable *
dbus_describe_params(struct pipeline_state *state)
{
    int i;
    GHashTable *h = cam_dbus_dict_new();

    for (i = 0; cam_dbus_params[i] != NULL; i++) {
        const struct pipeline_param *p = cam_dbus_params[i];
        GHashTable *desc = cam_dbus_dict_new();
        if (!desc) continue;
        cam_dbus_dict_add_boolean(desc, "get", TRUE);
        cam_dbus_dict_add_boolean(desc, "set", p->setter != NULL);
        cam_dbus_dict_add_boolean(desc, "notifies", (p->flags & PARAM_F_NOTIFY) != 0);
        cam_dbus_dict_take_boxed(h, p->name, CAM_DBUS_HASH_MAP, desc);
    }
    return h;
}
