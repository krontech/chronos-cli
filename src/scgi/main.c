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
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "jsmn.h"
#include "scgi.h"
#include "dbus-json.h"
#include "api/cam-rpc.h"

static struct scgi_ctx *ctx = NULL;

static void
sse_handler(struct scgi_conn *conn, const char *method, void *closure)
{
    if (conn->state == SCGI_STATE_SUBSCRIBE) {
        scgi_write_payload(conn, "%s", closure);
    }
}

/* D-Bus event handler */
void
scgi_signal_handler(DBusGProxy* proxy, GHashTable *args, const char *name)
{
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

void
scgi_allow_cors(struct scgi_conn *conn, const char *allowed)
{
    scgi_start_response(conn, 200, "OK");
    scgi_write_header(conn, "Access-Control-Allow-Origin: *");
    scgi_write_header(conn, "Access-Control-Allow-Methods: %s", allowed);
    scgi_write_header(conn, "Content-Type: application/json");
    scgi_write_header(conn, "Access-Control-Max-Age: %d", 2520);
    scgi_write_header(conn, "");
}

static void
scgi_subscribe(struct scgi_conn *conn, const char *method, void *user_data)
{
    /* Boilerplate to allow cross-origin requests */
    if (strcmp(method, "OPTIONS") == 0) {
        scgi_allow_cors(conn, "GET");
        return;
    }

    scgi_start_response(conn, 200, "OK");
    scgi_write_header(conn, "Content-type: text/event-stream");
    scgi_write_header(conn, "Cache-Control: no-cache");
    scgi_write_header(conn, "");
    conn->state = SCGI_STATE_SUBSCRIBE;
}

static void
scgi_property(struct scgi_conn *conn, const char *method, void *user_data)
{
    DBusGProxy* proxy = user_data;
    const char *path = scgi_header_find(conn, "PATH_INFO");
    const char *name;
    GError *error = NULL;
    GHashTable *h;
    gboolean okay;
    FILE *fp;
    char *json;
    size_t jslen;

    /* parse the path info for the property name */
    while (*path == '/') path++;
    if ((name = strchr(path, '/')) == NULL) {
        scgi_client_error(conn, 404, "Not Found");
        return;
    }
    while (*name == '/') name++;

    /* Boilerplate to allow cross-origin requests */
    if (strcmp(method, "OPTIONS") == 0) {
        scgi_allow_cors(conn, "GET");
        return;
    }

    /* Handle the requests by method. */
    if (strcmp(method, "GET") == 0) {
        GPtrArray *array = g_ptr_array_sized_new(1);
        if (!array) {
            scgi_client_error(conn, 500, "Internal Server Error");
            return;
        }
        g_ptr_array_add(array, (gpointer)name);

        /* Execute the D-Bus get call. */
        okay = dbus_g_proxy_call(proxy, "get", &error,
                dbus_g_type_get_collection("GPtrArray", G_TYPE_STRING), array, G_TYPE_INVALID,
                CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
        g_ptr_array_free(array, TRUE);
        if (!okay) {
            scgi_client_error(conn, 500, "Internal Server Error");
            return;
        }
    }
    /* TODO: Other Methods... PUT and POST seem obvious */
    else {
        scgi_write_header(conn, "Status: 405 Method Not Allowed");
        scgi_write_header(conn, "Allow: GET");
        scgi_write_header(conn, "");
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
    scgi_take_payload(conn, json, jslen);
}

static void
scgi_property_group(struct scgi_conn *conn, const char *method, void *user_data)
{
    DBusGProxy* proxy = user_data;
    GError *error = NULL;
    GHashTable *result;
    gboolean okay;
    FILE *fp;
    char *json;
    size_t jslen;

    /* Boilerplate to allow cross-origin requests */
    if (strcmp(method, "OPTIONS") == 0) {
        scgi_allow_cors(conn, "GET");
        return;
    }

    /* When processing a GET - generate the entire parameter set */
    if (strcmp(method, "GET") == 0) {
        GPtrArray *array;
        GHashTable *available;
        GHashTableIter iter;
        gpointer key, value;

        /* Request the list of parameters in the API */
        okay = dbus_g_proxy_call(proxy, "availableKeys", &error, G_TYPE_INVALID,
                CAM_DBUS_HASH_MAP, &available, G_TYPE_INVALID);
        if (!okay) {
            scgi_error_handler(conn, error);
            g_error_free(error);
            return;
        }

        /* Build a string array of all the keys */
        array = g_ptr_array_sized_new(g_hash_table_size(available));
        g_hash_table_iter_init(&iter, available);
        while (array && g_hash_table_iter_next(&iter, &key, &value)) {
            g_ptr_array_add(array, key);
        }

        /* Request the parameters from the API. */
        okay = dbus_g_proxy_call(proxy, "get", &error,
                dbus_g_type_get_collection("GPtrArray", G_TYPE_STRING), array, G_TYPE_INVALID,
                CAM_DBUS_HASH_MAP, &result, G_TYPE_INVALID);
        cam_dbus_dict_free(available);
        g_ptr_array_free(array, TRUE);
    }
    /* When processing a POST - take a dictionary of parameters and their new values */
    else if (strcmp(method, "POST") == 0) {
        GValue *params = scgi_parse_params(conn, method);
        if (!params) {
            scgi_client_error(conn, 400, "Bad Request");
            return;
        }

        /* PATH_INFO should be the D-Bus method name */
        okay = dbus_g_proxy_call(proxy, "set", &error,
                G_VALUE_TYPE(params), g_value_peek_pointer(params), G_TYPE_INVALID,
                CAM_DBUS_HASH_MAP, &result, G_TYPE_INVALID);
        g_free(params);
    }
    /* Otherwise, this method is not allowed. */
    else {
        scgi_start_response(conn, 405, "Method Not Allowed");
        scgi_write_header(conn, "Allowed: GET, OPTION");
        scgi_write_header(conn, "");
        return;
    }

    /* Check if the call succeeded */
    if (!okay) {
        scgi_error_handler(conn, error);
        g_error_free(error);
        return;
    }

    /* Render the D-Bus reply into JSON */
    if ((fp = open_memstream(&json, &jslen)) == NULL) {
        g_hash_table_destroy(result);
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }
    json_newline = "\r\n";
    json_printf_dict(fp, result, 0);
    fputs("\r\n", fp);
    fclose(fp);
    g_hash_table_destroy(result);
    
    /* Otherwise, keep testing... */
    scgi_start_response(conn, 200, "OK");
    scgi_write_header(conn, "Content-type: application/json");
    scgi_write_header(conn, "");
    scgi_take_payload(conn, json, jslen);
}

static void
scgi_describe(struct scgi_conn *conn, const char *method, void *user_data)
{
    DBusGProxy* proxy = user_data;
    GError *error = NULL;
    GHashTable *h;
    gboolean okay;
    FILE *fp;
    char *json;
    size_t jslen;

    /* Boilerplate to allow cross-origin requests */
    if (strcmp(method, "OPTIONS") == 0) {
        scgi_allow_cors(conn, "GET");
        return;
    }

    /* Request the list of parameters in the API */
    okay = dbus_g_proxy_call(proxy, "availableKeys", &error, G_TYPE_INVALID,
            CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
    if (!okay) {
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }

    /* TODO: Use D-Bus introspection on the API to get the list of calls */

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
    scgi_take_payload(conn, json, jslen);
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
    fprintf(fp, "\t-p, --port NUM list on TCP port NUM for SCGI requests\n");
    fprintf(fp, "\t-n, --control  connect to the control DBus interface\n");
    fprintf(fp, "\t-v, --video    connect to the video DBus interface\n");
    fprintf(fp, "\t-h, --help     display this help and exit\n");
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
    unsigned int scgi_port = 8111;
    const char *service = CAM_DBUS_CONTROL_SERVICE;
    const char *path = CAM_DBUS_CONTROL_PATH;
    const char *iface = CAM_DBUS_CONTROL_INTERFACE;
    const char *method;
    
    /* Option Parsing */
    const char *short_options = "p:nvh";
    const struct option long_options[] = {
        {"port",    required_argument, 0, 'p'},
        {"control", no_argument,       0, 'n'},
        {"video",   no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        char *end;
        switch (c) {
            case 'p':
                scgi_port = strtoul(optarg, &end, 10);
                if (!scgi_port || (*end != '\0')) {
                    fprintf(stderr, "Invalid port number given: %s\n", optarg);
                    return EXIT_FAILURE;
                }
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

    /* Initialize the GLib socket handler. */
    scgi_service = g_socket_service_new();
    g_socket_listener_add_inet_port((GSocketListener *)scgi_service, 8111, NULL, &error);
    if (error != NULL) {
        fprintf(stderr, "Failed to create listener socket: %s", error->message);
        exit(EXIT_FAILURE);
    }

    /* Listen for incomming connections */
    ctx = scgi_server_ctx(scgi_service, NULL, NULL);
    g_socket_service_start(scgi_service);

    /* Register the special SCGI path handlers */
    scgi_ctx_register(ctx, "subscribe", scgi_subscribe, proxy);
    scgi_ctx_register(ctx, "describe", scgi_describe, proxy);
    scgi_ctx_register(ctx, "p/[a-z]*", scgi_property, proxy);
    scgi_ctx_register(ctx, "p$", scgi_property_group, proxy);

    /* Add signal and call handlers by introspecting */
    scgi_introspect(ctx, bus, proxy);

    /* Run the loop until we exit. */
    g_main_loop_run(mainloop);
}
