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
#include <getopt.h>
#include <errno.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "jsmn.h"
#include "dbus-json.h"
#include "api/cam-rpc.h"

static unsigned int sse = 0;

static void
handler(DBusGProxy* proxy, GHashTable *args, gpointer user_data)
{
    const char *name = (const char *)user_data;

    if (sse) {
        json_newline = "\ndata:";
        fprintf(stdout, "event: %s\ndata:", name);
    }
    json_printf_dict(stdout, args, 0);
    fputs("\n\n", stdout);
    fflush(stdout);
} /* handler */

static void
usage(FILE *fp, int argc, char * const argv[])
{
    fprintf(fp, "Usage: %s [options]\n\n", argv[0]);

    fprintf(fp, "Listen for DBus signals from the Chronos camera daemons, and\n");
    fprintf(fp, "translate them into JSON. The resulting event stream can be\n");
    fprintf(fp, "encoded as JSON, or wrapped as HTML5 Server-Sent-Events.\n\n");

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-s, --sse     encode the signals as an HTML5 SSE stream\n");
    fprintf(fp, "\t-n, --control connect to the control DBus interface\n");
    fprintf(fp, "\t-v, --video   connect to the video DBus interface\n");
    fprintf(fp, "\t-h, --help    display this help and exit\n");
}

int
main(int argc, char * const argv[])
{
    DBusGConnection* bus;
    DBusGProxy* proxy;
    GMainLoop* mainloop;
    GError* error = NULL;
    gboolean okay;
    const char *service = CAM_DBUS_CONTROL_SERVICE;
    const char *path = CAM_DBUS_CONTROL_PATH;
    const char *iface = CAM_DBUS_CONTROL_INTERFACE;
    const char *method;
    
    /* Option Parsing */
    const char *short_options = "snvh";
    const struct option long_options[] = {
        {"sse",     no_argument,    0, 's'},
        {"control", no_argument,    0, 'n'},
        {"video",   no_argument,    0, 'v'},
        {"help",    no_argument,    0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 's':
                sse = 1;
                break;
            
            case 'v':
                service = CAM_DBUS_VIDEO_SERVICE;
                path = CAM_DBUS_VIDEO_PATH;
                iface = CAM_DBUS_VIDEO_INTERFACE;
                break;

            case 'n':
                service = CAM_DBUS_CONTROL_SERVICE;
                path = CAM_DBUS_CONTROL_PATH;
                iface = CAM_DBUS_CONTROL_INTERFACE;
                break;

            case 'h':
                usage(stdout, argc, argv);
                return EXIT_SUCCESS;
            case '?':
            default:
                return EXIT_FAILURE;
        }
    }
    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);

    /* Initialize the DBus system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        fprintf(stderr, "Failed to connect to system DBus\n");
        exit(EXIT_FAILURE);
    }
    proxy = dbus_g_proxy_new_for_name(bus, service, path, iface);
    if (proxy == NULL) {
        fprintf(stderr, "Failed to connect to %s\n", service);
        exit(EXIT_FAILURE);
    }

    /* Output CGI header data for HTML5 Server Sent Events*/
    if (sse) {
        fprintf(stdout, "Content-type: text/event-stream\n");
        fprintf(stdout, "\n");
    }

    /* Add signal handlers. */
    dbus_g_proxy_add_signal(proxy, "eof", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "sof", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "segment", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "notify", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    
    /* Connect signal handlers. */
    dbus_g_proxy_connect_signal(proxy, "eof", G_CALLBACK(handler), "eof", NULL);
    dbus_g_proxy_connect_signal(proxy, "sof", G_CALLBACK(handler), "sof", NULL);
    dbus_g_proxy_connect_signal(proxy, "segment", G_CALLBACK(handler), "segment", NULL);
    dbus_g_proxy_connect_signal(proxy, "notify", G_CALLBACK(handler), "notify", NULL);

    /* Run the loop until we exit. */
    g_main_loop_run(mainloop);
}
