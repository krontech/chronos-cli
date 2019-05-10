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

struct pipeline_param {
    const char *name;
    GType       type;
    /* Simple parameters - just read it directly out of state using the type. */
    size_t      offset;
    /* Complex parameters - callbacks to do translation and stateful stuff. */
    void        (*getter)(struct pipeline_state *state, const struct pipeline_param *param, GHashTable *data);
    gboolean    (*setter)(struct pipeline_state *state, const struct pipeline_param *param, GValue *val);
};

struct enumval focus_peak_colors[] = {
    {DISPLAY_CTL_FOCUS_PEAK_RED >> 6,       "red"},
    {DISPLAY_CTL_FOCUS_PEAK_GREEN >> 6,     "green"},
    {DISPLAY_CTL_FOCUS_PEAK_RED >> 6,       "blue"},
    {DISPLAY_CTL_FOCUS_PEAK_CYAN >> 6,      "cyan"},
    {DISPLAY_CTL_FOCUS_PEAK_MAGENTA >> 6,   "magenta"},
    {DISPLAY_CTL_FOCUS_PEAK_YELLOW >> 6,    "yellow"},
    {DISPLAY_CTL_FOCUS_PEAK_WHITE >> 6,     "white"},
    { 0, NULL }
};

static void
cam_video_state_getter(struct pipeline_state *state, const struct pipeline_param *param, GHashTable *data)
{
    switch (state->playstate) {
        case PLAYBACK_STATE_PAUSE:
        default:
            cam_dbus_dict_add_string(data, param->name, "paused");
            break;
        case PLAYBACK_STATE_LIVE:
            cam_dbus_dict_add_string(data, param->name, "live");
            break;
        case PLAYBACK_STATE_PLAY:
            cam_dbus_dict_add_string(data, param->name, "playback");
            break;
        case PLAYBACK_STATE_FILESAVE:
            cam_dbus_dict_add_string(data, param->name, "filesave");
            break;
    }
}

static void
cam_focus_peak_color_getter(struct pipeline_state *state, const struct pipeline_param *param, GHashTable *data)
{
    cam_dbus_dict_add_string(data, param->name, enumval_name(focus_peak_colors, state->config.peak_color >> 6, "disabled"));
}

static gboolean
cam_focus_peak_color_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    do {
        /* If we got a boolean, then select Cyan as the default color. */
        if (G_VALUE_HOLDS_BOOLEAN(val)) {
            state->config.peak_color = g_value_get_boolean(val) ? DISPLAY_CTL_FOCUS_PEAK_CYAN : 0;
            break;
        }
        /* Check for a string naming the color of choice. */
        if (G_VALUE_HOLDS_STRING(val)) {
            const char *color = g_value_get_string(val);
            switch (tolower(color[0])) {
                case 'r':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_RED;
                    break;
                case 'g':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_GREEN;
                    break;
                case 'b':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_BLUE;
                    break;
                case 'c':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_CYAN;
                    break;
                case 'm':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_MAGENTA;
                    break;
                case 'y':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_YELLOW;
                    break;
                case 'w':
                    state->config.peak_color = DISPLAY_CTL_FOCUS_PEAK_WHITE;
                    break;
                default:
                    state->config.peak_color = 0;
                    break;
            }
            break;
        }
        /* Although not recommended, also accept an integer. */
        if (G_VALUE_HOLDS_UINT(val)) {
            state->config.peak_color = (g_value_get_uint(val) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
            break;
        }
        if (G_VALUE_HOLDS_ULONG(val)) {
            state->config.peak_color = (g_value_get_ulong(val) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
            break;
        }
        if (G_VALUE_HOLDS_INT(val)) {
            state->config.peak_color = (g_value_get_int(val) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
            break;
        }
        if (G_VALUE_HOLDS_LONG(val)) {
            state->config.peak_color = (g_value_get_long(val) << 6) & DISPLAY_CTL_FOCUS_PEAK_COLOR;
            break;
        }
        /* Otherwise, it's not a sane focus peaking color. */
        return FALSE;
    } while (0);

    if (state->config.peak_color) {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_COLOR;
        state->control |= (DISPLAY_CTL_FOCUS_PEAK_ENABLE | state->config.peak_color);
    } else {
        state->control &= ~DISPLAY_CTL_FOCUS_PEAK_ENABLE;
    }

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
cam_focus_peak_level_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    /* Parse and set the focus peaking level. */
    double fplevel = g_value_get_double(val);
    if (fplevel < 0.0) fplevel = 0.0;
    if (fplevel > 1.0) fplevel = 1.0;
    state->config.peak_level = fplevel;

    /* Update the FPGA directly if already in live mode. */
    if (state->playstate == PLAYBACK_STATE_LIVE) {
        state->fpga->display->peaking_thresh = 35 - (20 * fplevel);
    }
    return TRUE;
}

static gboolean
cam_overlay_enable_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    state->overlay.enable = g_value_get_boolean(val);
    if (state->playstate == PLAYBACK_STATE_PLAY) overlay_setup(state);
    return TRUE;
}

static gboolean
cam_overlay_format_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    strncpy(state->overlay.format, g_value_get_string(val), sizeof(state->overlay.format));
    return TRUE;
}

static gboolean
cam_playback_position_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    state->position = g_value_get_long(val);
    return TRUE;
}

static gboolean
cam_playback_rate_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    state->playrate = g_value_get_long(val);
    return TRUE;
}

static gboolean
cam_playback_start_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    state->playstart = g_value_get_ulong(val);
    return TRUE;
}

static gboolean
cam_playback_length_setter(struct pipeline_state *state, const struct pipeline_param *param, GValue *val)
{
    state->playlength = g_value_get_ulong(val);
    return TRUE;
}

/* Table of parameters. */
static const struct pipeline_param cam_dbus_params[] = {
    { "videoState",         G_TYPE_STRING,  0, cam_video_state_getter, NULL},
    /* Exposure and focus aids. */
    { "overlayEnable",      G_TYPE_BOOLEAN, offsetof(struct pipeline_state, overlay.enable), NULL, cam_overlay_enable_setter},
    { "overlayFormat",      G_TYPE_STRING,  offsetof(struct pipeline_state, overlay.format), NULL, cam_overlay_format_setter},
    { "focusPeakingColor",  G_TYPE_STRING,  0, cam_focus_peak_color_getter, cam_focus_peak_color_setter},
    { "focusPeakingLevel",  G_TYPE_DOUBLE,  offsetof(struct pipeline_state, config.peak_level), NULL, cam_focus_peak_level_setter},
    { "zebraLevel",         G_TYPE_DOUBLE,  offsetof(struct pipeline_state, config.zebra_level), NULL, NULL},
    /* Playback position and rate. */
    { "playbackRate",       G_TYPE_LONG,    offsetof(struct pipeline_state, playrate), NULL, cam_playback_rate_setter},
    { "playbackPosition",   G_TYPE_LONG,    offsetof(struct pipeline_state, position), NULL, cam_playback_position_setter},
    { "playbackStart",      G_TYPE_ULONG,   offsetof(struct pipeline_state, playstart), NULL, cam_playback_start_setter},
    { "playbackLength",     G_TYPE_ULONG,   offsetof(struct pipeline_state, playlength), NULL, cam_playback_length_setter},
    /* Quantity of recorded video. */
    { "totalFrames",        G_TYPE_LONG,    offsetof(struct pipeline_state, seglist.totalframes), NULL, NULL},
    { "totalSegments",      G_TYPE_LONG,    offsetof(struct pipeline_state, seglist.totalsegs),   NULL, NULL},
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

        /* If a custom getter function exists - call it. */
        if (p->getter) {
            p->getter(state, p, data);
            return TRUE;
        }
        
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
            default:
                /* Unsupported type */
                return FALSE;
        }
    }
    /* Otherwise, no such parameter exists with that name. */
    return FALSE;
}

gboolean
dbus_set_param(struct pipeline_state *state, const char *name, GValue *gval)
{
    const struct pipeline_param *p;
    GValue xform;
    void *pvalue;

    for (p = cam_dbus_params; p->name; p++) {
        /* Look for a matching parameter by name. */
        if (strcasecmp(p->name, name) != 0) continue;
        if (!p->setter) return FALSE;

        /* Type transformation if necessary. */
        memset(&xform, 0, sizeof(xform));
        g_value_init(&xform, p->type);

        /* If the value holds the expected type - then set it. */
        if (G_VALUE_HOLDS(gval, p->type)) {
            return p->setter(state, p, gval);
        }
        /* Otherwise, try our best to transform the type and set it. */
        else if (g_value_transform(gval, &xform)) {
            return p->setter(state, p, &xform);
        }
        /* The user gave us a type we couldn't make sense of. */
        return FALSE;
    }
    /* Otherwise, no such parameter exists with that name. */
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
        /* TODO: Need more implementation for notifies. */
        cam_dbus_dict_add_boolean(desc, "notifies", FALSE);
        cam_dbus_dict_take_boxed(h, p->name, CAM_DBUS_HASH_MAP, desc);
    }
    return h;
}
