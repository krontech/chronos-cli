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
#include <gio/gio.h>
#include <dbus/dbus-glib.h>

#include "jsmn.h"
#include "scgi.h"
#include "dbus-json.h"
#include "api/cam-rpc.h"

static struct scgi_ctx *ctx = NULL;

static void
sse_handler(struct scgi_conn *conn, void *closure)
{
    if (conn->state == SCGI_STATE_SUBSCRIBE) {
        scgi_write_payload(conn, "%s", closure);
    }
}

/* D-Bus event handler */
static void
dbus_handler(DBusGProxy* proxy, GHashTable *args, gpointer user_data)
{
    const char *name = user_data;
    char *json;
    size_t jslen;
    FILE *fp = open_memstream(&json, &jslen);
    
    /* Write the JSON blob into a string. */
    json_newline = "\ndata:";
    fprintf(fp, "event: %s\ndata:", name);
    json_printf_dict(fp, args, 0);
    fputs("\n\n", fp);
    fclose(fp);

    /* Send it via the SSE streams. */
    scgi_ctx_foreach(ctx, sse_handler, json);
    free(json);
} /* dbus_handler */

/* This is a bit of a mess - handlers should be registered by path or something */
static void
scgi_handler(struct scgi_conn *conn, void *user_data)
{
    const char *path = scgi_header_find(conn, "PATH_INFO");
    const char *method = scgi_header_find(conn, "REQUEST_METHOD");
    DBusGProxy* proxy = user_data;
    GValue *params = NULL;
    GHashTable *h;
    gboolean okay;
    GError *error;
    FILE *fp;
    char *json;
    size_t jslen;

    if (!path || !method) {
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }

    /* Special Case for subscription */
    if (strcmp(path, "/subscribe") == 0) {
        scgi_start_response(conn, 200, "OK");
        scgi_write_header(conn, "Content-type: text/event-stream");
        scgi_write_header(conn, "");
        conn->state = SCGI_STATE_SUBSCRIBE;
        return;
    }

    /* Otherwise, turn POST into API calls. */
    if (strcmp(method, "POST") != 0) {
        scgi_client_error(conn, 405, "Method Not Allowed");
        return;
    }
    if (*path == '/') path++;

    /* Attempt to parse the method arguments from the POST data. */
    fprintf(stderr, "DEBUG: Got %d bytes of content\n", conn->contentlen);
    fwrite(conn->rx.buffer + conn->rx.offset, conn->contentlen, 1, stderr);
    params = json_parse(conn->rx.buffer + conn->rx.offset, conn->contentlen, NULL);
    if (!params) {
        scgi_client_error(conn, 400, "Bad Request");
        return;
    }

    /* PATH_INFO should be the D-Bus method name */
    okay = dbus_g_proxy_call(proxy, path, &error,
            G_VALUE_TYPE(params), g_value_peek_pointer(params), G_TYPE_INVALID,
            CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
    g_free(params);
    if (!okay) {
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }

    /* Render the D-Bus reply into JSON */
    if ((fp = open_memstream(&json, &jslen)) == NULL) {
        g_hash_table_destroy(h);
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }
    json_newline = "\r\n";
    json_printf_dict(fp, h, 0);
    fputs("\r\n", fp);
    fclose(fp);
    g_hash_table_destroy(h);
    
    /* Otherwise, keep testing... */
    scgi_start_response(conn, 200, "OK");
    scgi_write_header(conn, "Content-type: application/json");
    scgi_write_header(conn, "");
    scgi_write_payload(conn, "%s\r\n", json);

    /* Free the output buffer */
    free(json);
}

static void
usage(FILE *fp, int argc, char * const argv[])
{
    fprintf(fp, "Usage: %s [options]\n\n", argv[0]);

    fprintf(fp, "Simple-CGI web/D-Bus API service. This acts as a bridge between\n");
    fprintf(fp, "web requests delivered in SCGI format into D-Bus API calls into\n");
    fprintf(fp, "the camera's control system.\n\n");

    fprintf(fp, "API calls are made using the HTTP POST verb, with the parameters\n");
    fprintf(fp, "passed as the POST body in JSON encoding. With the PATH_INFO CGI\n");
    fprintf(fp, "variable used as the D-Bus method name.\n\n");

    fprintf(fp, "An additional path, \'/subscribe\', is also recognized, which returns\n");
    fprintf(fp, "an HTML5 Server-Sent-Event stream with the asynchronos signals from\n");
    fprintf(fp, "the D-Bus interface.\n\n");

    fprintf(fp, "options:\n");
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
    GSocketService *scgi_service;
    GError* error = NULL;
    gboolean okay;
    int scgi_port = 8111;
    const char *service = CAM_DBUS_CONTROL_SERVICE;
    const char *path = CAM_DBUS_CONTROL_PATH;
    const char *iface = CAM_DBUS_CONTROL_INTERFACE;
    const char *method;
    
    /* Option Parsing */
    const char *short_options = "nvh";
    const struct option long_options[] = {
        {"control", no_argument,    0, 'n'},
        {"video",   no_argument,    0, 'v'},
        {"help",    no_argument,    0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
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

    /* Initialize the GLib socket handler. */
    scgi_service = g_socket_service_new();
    g_socket_listener_add_inet_port((GSocketListener *)scgi_service, 8111, NULL, &error);
    if (error != NULL) {
        fprintf(stderr, "Failed to create listener socket: %s", error->message);
        exit(EXIT_FAILURE);
    }

    /* Listen for incomming connections */
    ctx = scgi_server_ctx(scgi_service, scgi_handler, proxy);
    g_socket_service_start(scgi_service);

    /* Add signal handlers. */
    dbus_g_proxy_add_signal(proxy, "eof", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "sof", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "segment", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "update", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "notify", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy, "complete", CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
    
    /* Connect signal handlers. */
    dbus_g_proxy_connect_signal(proxy, "eof", G_CALLBACK(dbus_handler), "eof", NULL);
    dbus_g_proxy_connect_signal(proxy, "sof", G_CALLBACK(dbus_handler), "sof", NULL);
    dbus_g_proxy_connect_signal(proxy, "segment", G_CALLBACK(dbus_handler), "segment", NULL);
    dbus_g_proxy_connect_signal(proxy, "update", G_CALLBACK(dbus_handler), "update", NULL);
    dbus_g_proxy_connect_signal(proxy, "notify", G_CALLBACK(dbus_handler), "notify", NULL);
    dbus_g_proxy_connect_signal(proxy, "complete", G_CALLBACK(dbus_handler), "complete", NULL);

    /* Run the loop until we exit. */
    g_main_loop_run(mainloop);
}
