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
#include "utils.h"
#include "api/cam-rpc.h"

#define PARAM_FLAG_NOTIFY   0x0001

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

static gboolean
cam_overlay_enable_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->overlay.enable = g_value_get_boolean(val);
    if (state->playstate == PLAYBACK_STATE_PLAY) overlay_setup(state);
    return TRUE;
}

static gboolean
cam_overlay_format_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    strncpy(state->overlay.format, g_value_get_string(val), sizeof(state->overlay.format));
    return TRUE;
}

static gboolean
cam_playback_position_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->position = g_value_get_long(val);
    return TRUE;
}

static gboolean
cam_playback_rate_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->playrate = g_value_get_long(val);
    return TRUE;
}

static gboolean
cam_playback_start_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->playstart = g_value_get_ulong(val);
    return TRUE;
}

static gboolean
cam_playback_length_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
{
    state->playlength = g_value_get_ulong(val);
    return TRUE;
}

/* This is really just here to keep the lines shorter. */
#define param_offset(_member_) offsetof(struct pipeline_state, _member_)

/* Table of parameters. */
static const struct pipeline_param cam_dbus_params[] = {
    { "videoState",         G_TYPE_ENUM,    PARAM_FLAG_NOTIFY, param_offset(playstate),         playback_states,    NULL},
    /* Exposure and focus aids. */
    { "overlayEnable",      G_TYPE_BOOLEAN, PARAM_FLAG_NOTIFY, param_offset(overlay.enable),    NULL,               cam_overlay_enable_setter},
    { "overlayFormat",      G_TYPE_STRING,  PARAM_FLAG_NOTIFY, param_offset(overlay.format),    NULL,               cam_overlay_format_setter},
    { "focusPeakingColor",  G_TYPE_ENUM,    PARAM_FLAG_NOTIFY, param_offset(config.peak_color), focus_peak_colors,  cam_focus_peak_color_setter},
    { "focusPeakingLevel",  G_TYPE_DOUBLE,  PARAM_FLAG_NOTIFY, param_offset(config.peak_level), NULL,               cam_focus_peak_level_setter},
    { "zebraLevel",         G_TYPE_DOUBLE,  PARAM_FLAG_NOTIFY, param_offset(config.zebra_level),NULL,               NULL},
    /* Playback position and rate. */
    { "playbackRate",       G_TYPE_LONG,    PARAM_FLAG_NOTIFY, param_offset(playrate),          NULL,               cam_playback_rate_setter},
    { "playbackPosition",   G_TYPE_LONG,    0,                 param_offset(position),          NULL,               cam_playback_position_setter},
    { "playbackStart",      G_TYPE_ULONG,   PARAM_FLAG_NOTIFY, param_offset(playstart),         NULL,               cam_playback_start_setter},
    { "playbackLength",     G_TYPE_ULONG,   PARAM_FLAG_NOTIFY, param_offset(playlength),        NULL,               cam_playback_length_setter},
    /* Quantity of recorded video. */
    { "totalFrames",        G_TYPE_LONG,    0,                 param_offset(seglist.totalframes), NULL,             NULL},
    { "totalSegments",      G_TYPE_LONG,    0,                 param_offset(seglist.totalsegs),   NULL,             NULL},
    { NULL, G_TYPE_INVALID, 0, 0, NULL, NULL}
};

gboolean
dbus_get_param(struct pipeline_state *state, const char *name, GHashTable *data)
{
    const struct pipeline_param *p;
    void *pvalue;

    for (p = cam_dbus_params; p->name; p++) {
        /* Look for a matching parameter by name. */
        if (strcasecmp(p->name, name) != 0) continue;

        /* Otherwise, parse the parameter out of the state structure. */
        pvalue = ((unsigned char *)state) + p->offset;
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
            default:
                /* Unsupported type */
                return FALSE;
        }
    }
    /* Otherwise, no such parameter exists with that name. */
    return FALSE;
}


static gboolean
dbus_set_enum(struct pipeline_state *state, const struct pipeline_param *p, GValue *gval, char *err)
{
    const struct enumval *e = p->extra;
    GValue gv_int = G_VALUE_INIT;
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
    const struct pipeline_param *p;
    GValue xform = G_VALUE_INIT;
    void *pvalue;

    for (p = cam_dbus_params; p->name; p++) {
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
    const struct pipeline_param *p;
    GHashTable *h = cam_dbus_dict_new();

    for (p = cam_dbus_params; p->name; p++) {
        GHashTable *desc = cam_dbus_dict_new();
        if (!desc) continue;
        cam_dbus_dict_add_boolean(desc, "get", TRUE);
        cam_dbus_dict_add_boolean(desc, "set", p->setter != NULL);
        cam_dbus_dict_add_boolean(desc, "notifies", (p->flags & PARAM_FLAG_NOTIFY) != 0);
        cam_dbus_dict_take_boxed(h, p->name, CAM_DBUS_HASH_MAP, desc);
    }
    return h;
}
