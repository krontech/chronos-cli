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
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "api/cam-rpc.h"
#include "camera.h"
#include "fpga-sensor.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */
static gboolean cam_dbus_get_video_settings(CamObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_set_video_settings(CamObject *cam, GHashTable *data, GError **error);
static gboolean cam_dbus_get_camera_data(CamObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_get_sensor_data(CamObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_get_timing_limits(CamObject *cam, gint hres, gint vres, GHashTable **data, GError *error);

#include "api/cam-dbus-server.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */
static void
cam_object_init(CamObject *cam)
{
    g_assert(cam != NULL);
    memset((char *)cam + sizeof(cam->parent), 0, sizeof(CamObject) - sizeof(cam->parent));
}

static void
cam_object_class_init(CamObjectClass *kclass)
{
    g_assert(kclass != NULL);
    dbus_g_object_type_install_info(CAM_OBJECT_TYPE, &dbus_glib_cam_dbus_object_info);
}

#define CAM_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_CAST((_obj_), CAM_OBJECT_TYPE, CamObject))
#define CAM_OBJECT_CLASS(_kclass_) \
    (G_TYPE_CHECK_CLASS_CAST((_kclass_), CAM_OBJECT_TYPE, CamObjectClass))
#define CAM_IS_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_TYPE(_obj_), CAM_OBJECT_TYPE))
#define CAM_IS_CLASS(_kclass_) \
    (G_TYPE_CHECK_CLASS_TYPE(_kclass_), CAM_OBJECT_TYPE))
#define CAM_GET_CLASS(_obj_) \
    (G_TYPE_INSTANCE_GET_CLASS(_obj_), CAM_OBJECT_TYPE, CamObjectClass))

G_DEFINE_TYPE(CamObject, cam_object, G_TYPE_OBJECT)

/*-------------------------------------
 * DBUS API Calls
 *-------------------------------------
 */
static gboolean
cam_dbus_get_camera_data(CamObject *cam, GHashTable **data, GError **error)
{
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        struct fpga *fpga = cam->fpga;
        cam_dbus_dict_add_string(dict, "model", "Chronos 1.4");
        cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
        cam_dbus_dict_add_printf(dict, "fpgaVersion", "%d.%d",
            fpga->reg[FPGA_VERSION], fpga->reg[FPGA_SUBVERSION]);
        cam_dbus_dict_add_printf(dict, "memoryGB", "%u", cam->mem_gbytes);
        if (strlen(cam->serial)) {
            cam_dbus_dict_add_string(dict, "serial", cam->serial);
        }
    }
    *data = dict;
    return (dict != NULL);
}

static gboolean
cam_dbus_get_video_settings(CamObject *cam, GHashTable **data, GError **error)
{
    GHashTable *dict = cam_dbus_dict_new();
    /* TODO: Implement Real Stuff */
    if (dict) {
        struct fpga *fpga = cam->fpga;
        cam_dbus_dict_add_uint(dict, "hRes", fpga->display->h_res);
        cam_dbus_dict_add_uint(dict, "vRes", fpga->display->v_res);
        /* TODO: ??? */
        cam_dbus_dict_add_uint(dict, "hOffset", 0);
        cam_dbus_dict_add_uint(dict, "vOffset", 0);
        cam_dbus_dict_add_uint(dict, "frameRate", 5);
        cam_dbus_dict_add_uint(dict, "exposureNsec", 6);
        cam_dbus_dict_add_uint(dict, "frameRate", 7);
        cam_dbus_dict_add_uint(dict, "gain", 8);
    }
    *data = dict;
    return (dict != NULL);
}

static gboolean
cam_dbus_set_video_settings(CamObject *cam, GHashTable *data, GError **error)
{
    /* TODO: Implement Real Stuff */
    g_hash_table_destroy(data);
    return TRUE;
}

static gboolean
cam_dbus_get_sensor_data(CamObject *cam, GHashTable **data, GError **error)
{
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        struct image_sensor *sensor = cam->sensor;
        char fourcc[5] = {
            (sensor->format >> 0) & 0xff,
            (sensor->format >> 8) & 0xff,
            (sensor->format >> 16) & 0xff,
            (sensor->format >> 24) & 0xff,
            '\0', 
        };
        cam_dbus_dict_add_string(dict, "name", sensor->name);
        if (strlen(fourcc)) cam_dbus_dict_add_string(dict, "pixelFormat", fourcc);

        cam_dbus_dict_add_uint(dict, "pixelRate", sensor->pixel_rate);
        cam_dbus_dict_add_uint(dict, "hMax", sensor->h_max_res);
        cam_dbus_dict_add_uint(dict, "vMax", sensor->v_max_res);
        cam_dbus_dict_add_uint(dict, "hMin", sensor->h_min_res);
        cam_dbus_dict_add_uint(dict, "vMin", sensor->v_min_res);
        cam_dbus_dict_add_uint(dict, "hIncrement", sensor->h_increment);
        cam_dbus_dict_add_uint(dict, "vIncrement", sensor->v_increment);
        cam_dbus_dict_add_uint(dict, "minExposureNsec", sensor->exp_min_nsec);
        cam_dbus_dict_add_uint(dict, "maxExposureNsec", sensor->exp_max_nsec);
    }
    *data = dict;
    return (dict != NULL);
}

static gboolean
cam_dbus_get_timing_limits(CamObject *cam, gint hres, gint vres, GHashTable **data, GError *error)
{
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {

    }
    *data = dict;
    return (dict != NULL);
}

/*-------------------------------------
 * The actual application.
 *-------------------------------------
 */
int
dbus_init(CamObject *cam)
{
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;

    GMainLoop* mainloop = NULL;
    guint result;
    GError* error = NULL;

    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    if (!mainloop) {
        perror("Main loop creation failed");
    }

    /* Bring up DBus and register with the system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        return -1;
    }

    /* Connect to the system dbus to register our service. */
    proxy = dbus_g_proxy_new_for_name(bus,
                                        DBUS_SERVICE_DBUS,
                                        DBUS_PATH_DBUS,
                                        DBUS_INTERFACE_DBUS);
    if (proxy == NULL) {
        g_printerr("Failed to get a proxy for D-Bus (%s)\n", error->message);
        return -1;
    }
    if (!dbus_g_proxy_call(proxy,
                            "RequestName",
                            &error,
                            G_TYPE_STRING, CAM_DBUS_SERVICE,
                            G_TYPE_UINT, 0,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &result,
                            G_TYPE_INVALID)) {
        g_printerr("D-Bus.RequstName RPC failed (%s)\n", error->message);
        return -1;
    }
    if (result != 1) {
        perror("D-Bus.RequstName call failed");
        return -1;
    }

    /* Create the camera object and register with the D-Bus system. */
    dbus_g_connection_register_g_object(bus, CAM_DBUS_PATH, G_OBJECT(cam));

    /* Run the loop until something interesting happens. */
    g_main_loop_run(mainloop);
}
