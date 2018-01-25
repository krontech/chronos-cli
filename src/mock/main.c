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
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dbus/dbus-glib.h>

#include "mock.h"
#include "api/cam-rpc.h"

int
main(void)
{
    MockObject  *mock;
    GMainLoop   *mainloop;
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;
    GError* error = NULL;
    guint result;

    /* Init glib */
    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    if (!mainloop) {
        perror("Main loop creation failed");
    }
    mock = g_object_new(MOCK_OBJECT_TYPE, NULL);

    /* Bring up DBus and register with the system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        return EXIT_FAILURE;
    }

    /* Connect to the system dbus to register our service. */
    proxy = dbus_g_proxy_new_for_name(bus, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);
    if (proxy == NULL) {
        g_printerr("Failed to get a proxy for D-Bus (%s)\n", error->message);
        return EXIT_FAILURE;
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
        return EXIT_FAILURE;
    }
    if (result != 1) {
        perror("D-Bus.RequstName call failed");
        return EXIT_FAILURE;
    }

    /* Create the camera object and register with the D-Bus system. */
    dbus_g_connection_register_g_object(bus, CAM_DBUS_PATH, G_OBJECT(mock));

    /* Run the loop until something interesting happens. */
    g_main_loop_run(mainloop);
}
