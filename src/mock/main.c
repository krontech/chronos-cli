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
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dbus/dbus-glib.h>

#include "mock.h"
#include "api/cam-rpc.h"
#include "api/cam-dbus-server.h"
#include "api/cam-dbus-video.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */
typedef struct {
    GObjectClass parent;
} MockControlClass;
typedef struct {
    GObjectClass parent;
} MockVideoClass;

GType mock_control_get_type(void);
GType mock_video_get_type(void);

#define MOCK_CONTROL_TYPE    (mock_control_get_type())
#define MOCK_VIDEO_TYPE      (mock_video_get_type())

static void
mock_control_init(MockControl *cmock)
{
    g_assert(cmock != NULL);
    cmock->state = NULL;
}
static void
mock_video_init(MockVideo *vmock)
{
    g_assert(vmock != NULL);
    vmock->state = NULL;
}

static void
mock_control_class_init(MockControlClass *cclass)
{
    g_assert(cclass != NULL);
    dbus_g_object_type_install_info(MOCK_CONTROL_TYPE, &dbus_glib_cam_control_object_info);
}

static void
mock_video_class_init(MockVideoClass *vclass)
{
    g_assert(vclass != NULL);
    dbus_g_object_type_install_info(MOCK_VIDEO_TYPE, &dbus_glib_cam_video_object_info);
}

G_DEFINE_TYPE(MockControl, mock_control, G_TYPE_OBJECT)
G_DEFINE_TYPE(MockVideo, mock_video, G_TYPE_OBJECT)

static void
mock_register(GObject *mock, DBusGConnection *bus, const char *service, const char *path)
{
    guint result;
    GError* error = NULL;
    DBusGProxy *proxy = dbus_g_proxy_new_for_name(bus, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);
    if (proxy == NULL) {
        g_printerr("Failed to get a proxy for D-Bus (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (!dbus_g_proxy_call(proxy,
                            "RequestName",
                            &error,
                            G_TYPE_STRING, service,
                            G_TYPE_UINT, 0,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &result,
                            G_TYPE_INVALID)) {
        g_printerr("D-Bus.RequstName RPC failed (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (result != 1) {
        g_printerr("D-Bus.RequstName call failed for %s\n", service);
        exit(EXIT_FAILURE);
    }
    dbus_g_connection_register_g_object(bus, path, mock);
    printf("Registered mock at %s\n", path);
}

int
main(void)
{
    MockControl *cmock;
    MockVideo   *vmock;
    GMainLoop   *mainloop;
    DBusGConnection *bus = NULL;
    GError* error = NULL;

    /* The fake camera state */
    struct mock_state state = {0};
    mock_sensor_init(&state.sensor);
    state.hres = state.sensor.h_max_res;
    state.vres = state.sensor.v_max_res;
    state.period_nsec = 1000000;
    state.exposure_nsec = state.period_nsec * 330 / 360;
    state.gain_db = 0;

    /* Init glib */
    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    if (!mainloop) {
        perror("Main loop creation failed");
    }
    cmock = g_object_new(MOCK_CONTROL_TYPE, NULL);
    vmock = g_object_new(MOCK_VIDEO_TYPE, NULL);
    cmock->state = &state;
    vmock->state = &state;

    /* Bring up DBus and register with the system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        return EXIT_FAILURE;
    }

    /* Connect to the system dbus to register our service. */
    mock_register(G_OBJECT(cmock), bus, CAM_DBUS_CONTROL_SERVICE, CAM_DBUS_CONTROL_PATH);
    mock_register(G_OBJECT(vmock), bus, CAM_DBUS_VIDEO_SERVICE, CAM_DBUS_VIDEO_PATH);

    /* Run the loop until something interesting happens. */
    g_main_loop_run(mainloop);
}
