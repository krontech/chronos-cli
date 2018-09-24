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
#ifndef __CAM_RPC_H
#define __CAM_RPC_H

#include <stdlib.h>
#include <stdint.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#define CAM_DBUS_CONTROL_SERVICE    "com.krontech.chronos.control"
#define CAM_DBUS_CONTROL_PATH       "/com/krontech/chronos/control"
#define CAM_DBUS_CONTROL_INTERFACE  "com.krontech.chronos.control"
#define CAM_DBUS_VIDEO_SERVICE      "com.krontech.chronos.video"
#define CAM_DBUS_VIDEO_PATH         "/com/krontech/chronos/video"
#define CAM_DBUS_VIDEO_INTERFACE    "com.krontech.chronos.video"

/* Error domains */
#define CAM_ERROR_PARAMETERS        g_quark_from_static_string("cam-parameters-quark")

/*-------------------------------------
 * DBus Hash Table Functions
 *-------------------------------------
 */
#define CAM_DBUS_HASH_MAP \
    dbus_g_type_get_map("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)

#define cam_dbus_dict_new() g_hash_table_new_full(g_str_hash, g_str_equal, g_free, cam_dbus_dict_cleanup)
#define cam_dbus_dict_free(_dict_) g_hash_table_destroy(_dict_)
#define cam_dbus_dict_add(_dict_, _name_, _gvalue_) \
    g_hash_table_insert(_dict_, g_strdup(_name_), _gvalue_);

static inline void
cam_dbus_dict_cleanup(gpointer ptr)
{
    g_value_unset(ptr);
    g_free(ptr);
}

static inline void
cam_dbus_dict_add_boolean(GHashTable *h, const char *name, gboolean value)
{
    GValue *gval;
    if (h && (gval = g_new0(GValue, 1))) {
        g_value_init(gval, G_TYPE_BOOLEAN);
        g_value_set_boolean(gval, value);
        cam_dbus_dict_add(h, name, gval);
    }
}

static inline void
cam_dbus_dict_add_int(GHashTable *h, const char *name, int value)
{
    GValue *gval;
    if (h && (gval = g_new0(GValue, 1))) {
        g_value_init(gval, G_TYPE_INT);
        g_value_set_int(gval, value);
        cam_dbus_dict_add(h, name, gval);
    }
}

static inline void
cam_dbus_dict_add_uint(GHashTable *h, const char *name, unsigned int value)
{
    GValue *gval;
    if (h && (gval = g_new0(GValue, 1))) {
        g_value_init(gval, G_TYPE_UINT);
        g_value_set_uint(gval, value);
        cam_dbus_dict_add(h, name, gval);
    }
}

static inline void
cam_dbus_dict_add_float(GHashTable *h, const char *name, double value)
{
    GValue *gval;
    if (h && (gval = g_new0(GValue, 1))) {
        g_value_init(gval, G_TYPE_DOUBLE);
        g_value_set_double(gval, value);
        cam_dbus_dict_add(h, name, gval);
    }
}

static inline void
cam_dbus_dict_add_string(GHashTable *h, const char *name, const char *value)
{
    GValue *gval;
    if (h && (gval = g_new0(GValue, 1))) {
        g_value_init(gval, G_TYPE_STRING);
        g_value_set_string(gval, value);
        cam_dbus_dict_add(h, name, gval);
    }
}

static inline void
cam_dbus_dict_add_printf(GHashTable *h, const char *name, const char *fmt, ...)
{
    GValue *gval;
    if (h && (gval = g_new0(GValue, 1))) {
        va_list ap;
        va_start(ap, fmt);
        gchar *s = g_strdup_vprintf(fmt, ap);
        va_end(ap);
        g_value_init(gval, G_TYPE_STRING);
        if (s) {
            g_value_take_string(gval, s);
            cam_dbus_dict_add(h, name, gval);
        } else {
            g_free(gval);
        }
    }
}

static inline gboolean
cam_dbus_dict_exists(GHashTable *h, const char *name)
{
    return (g_hash_table_lookup(h, name) != NULL);
}

static inline gboolean
cam_dbus_dict_get_boolean(GHashTable *h, const char *name, gboolean defval)
{
    gpointer x = g_hash_table_lookup(h, name);
    if (x && G_VALUE_HOLDS_BOOLEAN(x)) return g_value_get_boolean(x);
    return defval;
}

static inline long
cam_dbus_dict_get_int(GHashTable *h, const char *name, long defval)
{
    gpointer x = g_hash_table_lookup(h, name);
    if (x && G_VALUE_HOLDS_INT(x)) return g_value_get_int(x);
    if (x && G_VALUE_HOLDS_LONG(x)) return g_value_get_long(x);
    if (x && G_VALUE_HOLDS_UINT(x)) return g_value_get_uint(x);
    if (x && G_VALUE_HOLDS_ULONG(x)) return g_value_get_ulong(x);
    return defval;
}

static inline unsigned long
cam_dbus_dict_get_uint(GHashTable *h, const char *name, unsigned long defval)
{
    gpointer x = g_hash_table_lookup(h, name);
    if (x && G_VALUE_HOLDS_UINT(x)) return g_value_get_uint(x);
    if (x && G_VALUE_HOLDS_ULONG(x)) return g_value_get_ulong(x);
    if (x && G_VALUE_HOLDS_INT(x) && (g_value_get_int(x) >= 0)) return g_value_get_int(x);
    if (x && G_VALUE_HOLDS_LONG(x) && (g_value_get_long(x) >= 0)) return g_value_get_long(x);
    return defval;
}

static inline double
cam_dbus_dict_get_float(GHashTable *h, const char *name, double defval)
{
    gpointer x = g_hash_table_lookup(h, name);
    if (x && G_VALUE_HOLDS_FLOAT(x)) return g_value_get_float(x);
    if (x && G_VALUE_HOLDS_DOUBLE(x)) return g_value_get_double(x);
    /* Also allow integer-float conversions */
    if (x && G_VALUE_HOLDS_INT(x)) return g_value_get_int(x);
    if (x && G_VALUE_HOLDS_LONG(x)) return g_value_get_long(x);
    if (x && G_VALUE_HOLDS_UINT(x)) return g_value_get_uint(x);
    if (x && G_VALUE_HOLDS_ULONG(x)) return g_value_get_ulong(x);
    return defval;
}

static inline const char *
cam_dbus_dict_get_string(GHashTable *h, const char *name, const char *defval)
{
    gpointer x = g_hash_table_lookup(h, name);
    return (x && G_VALUE_HOLDS_STRING(x)) ? g_value_get_string(x) : defval;
}

void cam_dbus_dict_to_json(GHashTable *h, FILE *fp);

/*-------------------------------------
 * Camera Client API
 *-------------------------------------
 */
struct cam_video_settings {
    unsigned long   hres;
    unsigned long   vres;
    unsigned long   hoffset;
    unsigned long   voffset;
    unsigned long   exposure;
    unsigned long   framerate;
    unsigned int    gain;
};

#define CAM_SENSOR_MONOCHROME       (1 << 0)
struct cam_sensor_data {
    char            name[64];
    uint32_t        flags;
    unsigned long   hmax;
    unsigned long   vmax;
    unsigned long   hmin;
    unsigned long   vmin;
    unsigned long   hincrement;
    unsigned long   vincrement;
    unsigned long   expmin; /* ns */
    unsigned long   expmax; /* ns */
    unsigned int    n_gains;
    int             gains[8]; /* dB */
};

/* Camera Trigger Types */
#define CAM_TRIGGER_ANALOG      0
#define CAM_TRIGGER_ISOLATED    1
/* Trigger Actions */
#define CAM_TRIGGER_ACTION      (0xff << 0)
#define CAM_TRIGGER_DISABLED    (0 << 0)
#define CAM_TRIGGER_RECORD      (1 << 1)
#define CAM_TRIGGER_EXPOSURE    (1 << 2)
/* Trigger Flags */
#define CAM_TRIGGER_INVERT      (1 << 8)
#define CAM_TRIGGER_DEBOUNCE    (1 << 9)
#define CAM_TRIGGER_PULLUP      (1 << 10)
#define CAM_TRIGGER_PULLUP_20MA (1 << 11)
struct cam_trigger {
    uint8_t     type;
    uint32_t    flags;
    uint32_t    vref; /* mV */
};

/* Camera Events */
#define CAM_EVENT_RECORD_START  1
#define CAM_EVENT_RECORD_STOP   2

/* Client API calls */
DBusGProxy *cam_proxy_new(void);
int cam_get_video_settings(DBusGProxy *proxy, struct cam_video_settings *s);
int cam_set_video_settings(DBusGProxy *proxy, const struct cam_video_settings *s);
int cam_get_sensor_data(DBusGProxy *proxy, struct cam_sensor_data *data);
int cam_get_trigger_info(DBusGProxy *proxy, unsigned int trigid, struct cam_trigger *trig);
int cam_get_trigger_config(DBusGProxy *proxy, unsigned int trigid, struct cam_trigger *trig);
int cam_set_trigger_config(DBusGProxy *proxy, unsigned int trigid, const struct cam_trigger *trig);

#endif /* __CAM_RPC_H */
