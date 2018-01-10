#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "api/cam-rpc.h"
#include "api/cam-dbus-client.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_x_) ((sizeof(_x_)/sizeof((_x_)[0])))
#endif

DBusGProxy *
cam_proxy_new(void)
{
    DBusGConnection* bus;
    DBusGProxy* proxy;
    GError* error = NULL;
    
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        return NULL;
    }
    proxy = dbus_g_proxy_new_for_name(bus, CAM_DBUS_SERVICE, CAM_DBUS_PATH, CAM_DBUS_INTERFACE);
    if (proxy == NULL) {
        perror("Failed to get a proxy for D-Bus\n");
        return NULL;
    }
    return proxy;
}

int
cam_get_video_settings(DBusGProxy *proxy, struct cam_video_settings *s)
{
    GHashTable *h;
    GError* error = NULL;

    com_krontech_chronos_control_get_video_settings(proxy, &h, &error);
    if (error != NULL) {
        g_error_free(error);
        return -1;
    }
    s->hres = cam_dbus_dict_get_uint(h, "hRes", 0);
    s->vres = cam_dbus_dict_get_uint(h, "vRes", 0);
    s->hoffset = cam_dbus_dict_get_uint(h, "hOffset", 0);
    s->voffset = cam_dbus_dict_get_uint(h, "vOffset", 0);
    s->exposure = cam_dbus_dict_get_uint(h, "exposureNsec", 0);
    s->framerate = cam_dbus_dict_get_uint(h, "frameRate", 0);
    s->gain = cam_dbus_dict_get_uint(h, "gain", 0);
    g_hash_table_destroy(h);
    return 0;
}

int
cam_set_video_settings(DBusGProxy *proxy, const struct cam_video_settings *s)
{
    GError* error = NULL;
    GHashTable *h = cam_dbus_dict_new();
    cam_dbus_dict_add_uint(h, "hRes", s->hres);
    cam_dbus_dict_add_uint(h, "vRes", s->vres);
    cam_dbus_dict_add_uint(h, "hOffset", s->hoffset);
    cam_dbus_dict_add_uint(h, "vOffset", s->voffset);
    cam_dbus_dict_add_uint(h, "frameRate", s->framerate);
    cam_dbus_dict_add_uint(h, "exposureNsec", s->exposure);
    cam_dbus_dict_add_uint(h, "frameRate", s->exposure);
    cam_dbus_dict_add_uint(h, "gain", s->gain);

    com_krontech_chronos_control_set_video_settings(proxy, h, &error);
    g_hash_table_destroy(h);
    if (error != NULL) {
        g_error_free(error);
        return -1;
    }
    return 0;
}

int
cam_get_sensor_data(DBusGProxy *proxy, struct cam_sensor_data *data)
{ 
    char *name;
    GError* error = NULL;
    GHashTable *h;

    com_krontech_chronos_control_get_sensor_data(proxy, &h, &error);
    if (error != NULL) {
        g_error_free(error);
        return -1;
    }
    data->flags = 0;
    strncpy(data->name, cam_dbus_dict_get_string(h, "name", "Unknown"), sizeof(data->name));
    if (cam_dbus_dict_get_boolean(h, "monochrome")) data->flags |= CAM_SENSOR_MONOCHROME;
    data->hmax = cam_dbus_dict_get_uint(h, "hMax", 0);
    data->vmax = cam_dbus_dict_get_uint(h, "vMax", 0);
    data->hmin = cam_dbus_dict_get_uint(h, "hMin", 0);
    data->vmin = cam_dbus_dict_get_uint(h, "vMin", 0);
    data->hincrement = cam_dbus_dict_get_uint(h, "hIncrement", 0);
    data->vincrement = cam_dbus_dict_get_uint(h, "vIncrement", 0);
    data->expmax = cam_dbus_dict_get_uint(h, "minExposureNsec", 0);
    data->expmin = cam_dbus_dict_get_uint(h, "maxExposureNsec", 0);
    /* TODO: How to send gain? */
    data->n_gains = 0;
    g_hash_table_destroy(h);
    return 0;
}

int cam_get_trigger_info(DBusGProxy *proxy, unsigned int trigid, struct cam_trigger *trig);
int cam_get_trigger_config(DBusGProxy *proxy, unsigned int trigid, struct cam_trigger *trig);
int cam_set_trigger_config(DBusGProxy *proxy, unsigned int trigid, const struct cam_trigger *trig);
