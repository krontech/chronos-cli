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

/* Parse the request arguments, accepting one of two formats:
 *  - application/json as an HTTP PUT or POST body.
 *  - QUERY_STRING when passed as an HTTP GET.
 * 
 * TODO: Add support for form-encoded data similar to the QUERY_STRING.
 *
 * Returns a GValue suitable for passing into a D-Bus call, or NULL on error.
 */
GValue *
scgi_parse_params(struct scgi_conn *conn, const char *method)
{
    /* GET requests can pass arguments via query strings */
    if (strcmp(method, "GET") == 0) {
        GHashTable *h = cam_dbus_dict_new();
        GValue *gval;
        char *query = (char *)scgi_header_find(conn, "QUERY_STRING");
        if (!query) query = "";

        /* Parse the query string into a simple dictionary. */
        while (*query) {
            char *name = query;
            char *sep = strchr(query, '&');
            char *value;
            char *end;
            size_t len;

            /* Parse out the query parameters */
            if (sep) {
                *sep++ = '\0';
                query = sep;
            }
            else query = "";
            scgi_urldecode(name);

            /* Check if the query string also set a value. */
            value = strchr(name, '=');
            if (!value) {
                /* For empty parameters, just set name=true */
                cam_dbus_dict_add_boolean(h, name, TRUE);
                continue;
            }
            
            /* Decode the value */
            *value++ = '\0';
            scgi_urldecode(value);
            if (strcmp(value, "true") == 0) {
                cam_dbus_dict_add_boolean(h, name, TRUE);
                continue;
            }
            else if (strcmp(value, "false") == 0) {
                cam_dbus_dict_add_boolean(h, name, FALSE);
                continue;
            }

            /* Try parsing numeric types */
            long longval = strtol(value, &end, 10);
            if (*end == '\0') {
                cam_dbus_dict_add_int(h, name, longval);
                continue;
            }
            double floatval = strtod(value, &end);
            if (*end == '\0') {
                cam_dbus_dict_add_float(h, name, floatval);
                continue;
            }

            /* Otherwise, treat it like a string. */
            cam_dbus_dict_add_printf(h, name, "%s", value);
        }

        /* Allocate a GValue for the result. */
        if ((gval = g_new0(GValue, 1)) == NULL) {
            cam_dbus_dict_free(h);
            return NULL;
        }
        g_value_init(gval, CAM_DBUS_HASH_MAP);
        g_value_take_boxed(gval, h);
        return gval;
    }
    else if ((strcmp(method, "POST") != 0) && (strcmp(method, "PUT") != 0)) {
        /* Method is not allowed. */
        return NULL;
    }
    else if (conn->contentlen == 0) {
        /* If there was no request body, then this would be a void call. */
        GValue *gval = g_new0(GValue, 1);
        if (gval) g_value_init(gval, G_TYPE_NONE);
        return gval;
    }
    else {
        /* Otherwise, we expect a JSON request */
        return json_parse(conn->rx.body, conn->contentlen, NULL);
    }
}

void
scgi_error_handler(struct scgi_conn *conn, GError *err)
{
    const char *name;

    /* The only error we are actually expecting here is a DBusError */
    if (err->domain != dbus_g_error_quark()) {
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }

    /* Turn the D-Bus error code into an HTTP error code */
    switch (err->code) {
        case DBUS_GERROR_INVALID_ARGS:
            scgi_start_response(conn, 400, "Bad Request");
            break;

        case DBUS_GERROR_UNKNOWN_METHOD:
            scgi_start_response(conn, 404, "File Not Found");
            break;

        case DBUS_GERROR_SERVICE_UNKNOWN:
            scgi_start_response(conn, 503, "Service Unavailable");
            break;

        case DBUS_GERROR_NO_REPLY:
        case DBUS_GERROR_TIMEOUT:
        case DBUS_GERROR_TIMED_OUT:
            scgi_start_response(conn, 504, "Gateway Timeout");
            break;

        case DBUS_GERROR_FAILED:
        case DBUS_GERROR_NO_MEMORY:
        case DBUS_GERROR_NAME_HAS_NO_OWNER:
        case DBUS_GERROR_IO_ERROR:
        case DBUS_GERROR_BAD_ADDRESS:
        case DBUS_GERROR_NOT_SUPPORTED:
        case DBUS_GERROR_LIMITS_EXCEEDED:
        case DBUS_GERROR_ACCESS_DENIED:
        case DBUS_GERROR_AUTH_FAILED:
        case DBUS_GERROR_NO_SERVER:
        case DBUS_GERROR_NO_NETWORK:
        case DBUS_GERROR_ADDRESS_IN_USE:
        case DBUS_GERROR_DISCONNECTED:
        case DBUS_GERROR_FILE_NOT_FOUND:
        case DBUS_GERROR_FILE_EXISTS:
        case DBUS_GERROR_MATCH_RULE_NOT_FOUND:
        case DBUS_GERROR_MATCH_RULE_INVALID:
        case DBUS_GERROR_SPAWN_EXEC_FAILED:
        case DBUS_GERROR_SPAWN_FORK_FAILED:
        case DBUS_GERROR_SPAWN_CHILD_EXITED:
        case DBUS_GERROR_SPAWN_CHILD_SIGNALED:
        case DBUS_GERROR_SPAWN_FAILED:
        case DBUS_GERROR_UNIX_PROCESS_ID_UNKNOWN:
        case DBUS_GERROR_INVALID_SIGNATURE:
        case DBUS_GERROR_INVALID_FILE_CONTENT:
        case DBUS_GERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN:
        case DBUS_GERROR_REMOTE_EXCEPTION:
        default:
            scgi_start_response(conn, 500, "Internal Server Error");
            break;
    }

    scgi_write_header(conn, "Content-Type: text/plain");
    scgi_write_header(conn, "X-Debug-Code: %d", err->code);
    scgi_write_header(conn, "");
    scgi_write_payload(conn, "%s", err->message);
}

void
scgi_call_void(struct scgi_conn *conn, const char *method, void *user_data)
{
    const char *path = scgi_header_find(conn, "PATH_INFO");
    DBusGProxy* proxy = user_data;
    GError *error = NULL;
    GHashTable *h;
    gboolean okay;
    FILE *fp;
    char *json;
    size_t jslen;

    if (!path) {
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }
    if (*path == '/') path++;

    /* Boilerplate to allow cross-origin requests */
    if (strcmp(method, "OPTIONS") == 0) {
        scgi_start_response(conn, 200, "OK");
        scgi_write_header(conn, "Access-Control-Allow-Origin: *");
        scgi_write_header(conn, "Access-Control-Allow-Methods: GET, OPTION");
        scgi_write_header(conn, "Content-Type: application/json");
        scgi_write_header(conn, "Access-Control-Max-Age: %d", 2520);
        scgi_write_header(conn, "");
        return;
    }

    /* PATH_INFO should be the D-Bus method name */
    okay = dbus_g_proxy_call(proxy, path, &error, G_TYPE_INVALID,
            CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
    if (!okay) {
        scgi_error_handler(conn, error);
        g_error_free(error);
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

void
scgi_call_args(struct scgi_conn *conn, const char *method, void *user_data)
{
    const char *path = scgi_header_find(conn, "PATH_INFO");
    DBusGProxy* proxy = user_data;
    GError *error = NULL;
    GValue *params;
    GHashTable *h;
    gboolean okay;
    FILE *fp;
    char *json;
    size_t jslen;

    if (!path) {
        scgi_client_error(conn, 500, "Internal Server Error");
        return;
    }
    if (*path == '/') path++;

    /* Boilerplate to allow cross-origin requests */
    if (strcmp(method, "OPTIONS") == 0) {
        scgi_start_response(conn, 200, "OK");
        scgi_write_header(conn, "Access-Control-Allow-Origin: *");
        scgi_write_header(conn, "Access-Control-Allow-Methods: GET, POST, OPTION");
        scgi_write_header(conn, "Access-Control-Allow-Headers: Content-Type");
        scgi_write_header(conn, "Content-Type: application/json");
        scgi_write_header(conn, "Access-Control-Max-Age: %d", 2520);
        scgi_write_header(conn, "");
        return;
    }

    /* Attempt to parse the method arguments from the POST data. */
    params = scgi_parse_params(conn, method);
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
        scgi_error_handler(conn, error);
        g_error_free(error);
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
