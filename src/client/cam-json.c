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

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "dbus-json.h"
#include "api/cam-rpc.h"

#define OPT_FLAG_JSRPC  (1<<0)
#define OPT_FLAG_CGI    (1<<1)
#define OPT_FLAG_GET    (1<<2)

static const char *
jsonrpc_err_message(int code)
{
    switch (code) {
        case JSONRPC_ERR_PARSE_ERROR:
            return "Invalid JSON";
        case JSONRPC_ERR_INVALID_REQUEST:
            return "Invalid Request";
        case JSONRPC_ERR_METHOD_NOT_FOUND:
            return "Method Not Found";
        case JSONRPC_ERR_INVALID_PARAMETERS:
            return "Invalid Parameters";
        case JSONRPC_ERR_INTERNAL_ERROR:
            return "Internal Error";
        case JSONRPC_ERR_SERVER_ERROR:
            return "Internal Server Error";
        default:
            return "Unknown Error";
    }
}

static void
handle_error(int code, const char *message, unsigned long flags)
{
    if (flags & OPT_FLAG_CGI) {
        fprintf(stdout, "Content-type: application/json\n");
        switch (code) {
            case JSONRPC_ERR_PARSE_ERROR:
            case JSONRPC_ERR_INVALID_REQUEST:
            case JSONRPC_ERR_INVALID_PARAMETERS:
                fprintf(stdout, "Status: 400 Bad Request\n");
                break;

            case JSONRPC_ERR_METHOD_NOT_FOUND:
                fprintf(stdout, "Status: 404 Not Found\n");
                break;
            
            case JSONRPC_ERR_INTERNAL_ERROR:
            case JSONRPC_ERR_SERVER_ERROR:
                fprintf(stdout, "Status: 500 Internal Server Error\n");
        }
        fputs("\n{\n", stdout);
        fprintf(stdout, "%*.s\"error\": ", JSON_TAB_SIZE, "");
        json_printf_utf8(stdout, message);
        fprintf(stdout, "\n}\n");
    }
    else {
        fprintf(stderr, "RPC Call Failed: %s\n", message);
    }
    exit(EXIT_FAILURE);
}

static void
cleanup_socket(void)
{
    struct sockaddr_un addr;
    sprintf(addr.sun_path, "/tmp/cam-json-%d.sock", getpid());
    unlink(addr.sun_path);
}

static void
usage(FILE *fp, int argc, char * const argv[])
{
    fprintf(fp, "Usage: %s [options] [METHOD [PARAMS]]\n\n", argv[0]);

    fprintf(fp, "Make a DBus call to the Chronos camera daemon, and translate\n");
    fprintf(fp, "the result into JSON.\n\n");

    fprintf(fp, "In normal and JSON-RPC mode, the RPC method is provided as the\n");
    fprintf(fp, "first positional argument. If the method takes arguments, they\n");
    fprintf(fp, "will be parsed from the optional PARAMS file (or \'-\' to read\n");
    fprintf(fp, "from stdin).\n\n");

    fprintf(fp, "In CGI mode, the PATH_INFO environment variable is parsed for\n");
    fprintf(fp, "the RPC method to call and arguments will be parsed from stdin.\n\n");
    
    fprintf(fp, "In get mode, the \'get\' method will be called to retrieive\n");
    fprintf(fp, "parameters from the DBus interface. The names of the parameters\n");
    fprintf(fp, "to retrieve are provided as the positional arguments to %s.\n\n", argv[0]);

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-c, --cgi        encode the results in CGI/1.0 format\n");
    fprintf(fp, "\t-g, --get        get paramereters from the DBus interface\n");
    fprintf(fp, "\t-j, --jsrpc SOCK Use JSON-RPC over a UNIX datagram socket at SOCK\n");
    fprintf(fp, "\t-n, --control    connect to the control DBus interface\n");
    fprintf(fp, "\t-v, --video      connect to the video DBus interface\n");
    fprintf(fp, "\t-h, --help       display this help and exit\n");
}

int
main(int argc, char * const argv[])
{
    GHashTable *h;
    GValue *params = NULL;
    GError* error = NULL;
    const char *service = CAM_DBUS_CONTROL_SERVICE;
    const char *path = CAM_DBUS_CONTROL_PATH;
    const char *iface = CAM_DBUS_CONTROL_INTERFACE;
    const char *sockname = NULL;
    const char *method;
    unsigned long flags = 0;
    
    /* Option Parsing */
    const char *short_options = "j:cgvnh";
    const struct option long_options[] = {
        {"jsrpc",   required_argument,  0, 'j'},
        {"cgi",     no_argument,        0, 'c'},
        {"get",     no_argument,        0, 'g'},
        {"control", no_argument,        0, 'n'},
        {"video",   no_argument,        0, 'v'},
        {"help",    no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 'j':
                flags |= OPT_FLAG_JSRPC;
                sockname = optarg;
                break;
            
            case 'c':
                flags |= OPT_FLAG_CGI;
                break;
            
            case 'g':
                flags |= OPT_FLAG_GET;
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

    /*
     * If getting parameters, the method is 'get' and the positional
     * arguments are the names of the parameters to get.
     */
    if (flags & OPT_FLAG_GET) {
        GPtrArray *array = g_ptr_array_sized_new(argc - optind);
        method = "get";
        params = g_new0(GValue, 1);
        if (!array && !params) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        g_value_init(params, dbus_g_type_get_collection("GPtrArray", G_TYPE_STRING));
        g_ptr_array_set_free_func(array, g_free);
        while (optind < argc) {
            g_ptr_array_add(array, g_strdup(argv[optind++]));
        }
        g_value_take_boxed(params, array);
    }
    /* If CGI, get the requested method from the PATH_INFO variable. */
    else if (flags & OPT_FLAG_CGI) {
        int err;

        method = getenv("PATH_INFO");
        if (!method) {
            fprintf(stderr, "Missing variable: PATH_INFO\n");
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return EXIT_FAILURE;
        }
        /* Stip leading slashes from PATH_INFO */
        while (*method == '/') method++;

        /* Attempt to parse the method arguments from stdin. */
        params = json_parse_file(stdin, &err);
        if (err != 0) {
            handle_error(err, jsonrpc_err_message(err), flags);
        }
    }
    /* Otherwise, the method name is passed in via the command line. */
    else if (optind >= argc) {
        fprintf(stderr, "Missing argument: METHOD\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }
    else {
        method = argv[optind++];

        /* If yet another parameter is present, it may provide a source file for
         * the RPC request parameters, or it may be '-' to read paramers from stdin.
         */
        if (optind < argc) {
            int err;
            const char *filename = argv[optind++];
            FILE *fp = strcmp(filename, "-") ? fopen(filename, "r") : stdin;
            if (!fp) {
                fprintf(stderr, "Failed to open '%s' for reading: %s\n", filename, strerror(errno));
                return EXIT_FAILURE;
            }
            params = json_parse_file(fp, &err);
            fclose(fp);
            if (err != 0) {
                handle_error(err, jsonrpc_err_message(err), flags);
            }
        }
    }

    /* Initialize the JSON-RPC socket. */
    if (flags & OPT_FLAG_JSRPC) {
        char msgbuf[16384];
        int sock;
        int msglen;
        int errcode;
        struct sockaddr_un addr;
        FILE *fp;
        char *json;
        size_t jslen;
        GValue *gval;

        /* Bind to a local socket name to get the reply. */
        sock = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (sock < 0) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        addr.sun_family = AF_UNIX;
        sprintf(addr.sun_path, "/tmp/cam-json-%d.sock", getpid());
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        atexit(cleanup_socket);

        /* Create the JSON-RPC request */
        h = cam_dbus_dict_new();
        cam_dbus_dict_add_string(h, "jsonrpc", "2.0");
        cam_dbus_dict_add_string(h, "method", method);
        cam_dbus_dict_add_uint(h, "id", getpid());
        if (params) {
            cam_dbus_dict_take_boxed(h, "params", G_VALUE_TYPE(params), g_value_peek_pointer(params));
        }

        /* Render the D-Bus request into JSON */
        if ((fp = open_memstream(&json, &jslen)) == NULL) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        json_newline = "\r\n";
        json_printf_dict(fp, h, 0);
        fputs("\r\n", fp);
        fclose(fp);
        g_hash_table_destroy(h);

        /* Send the JSON-RPC request to the destination socket. */
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, sockname);
        if (sendto(sock, json, jslen, 0, (struct sockaddr *)&addr, sizeof(addr)) < jslen) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        free(json);

        /* Wait for the reply from the server */
        jslen = recv(sock, msgbuf, sizeof(msgbuf), 0);
        close(sock);
        if (jslen <= 0) {

        }
        gval = json_parse(msgbuf, jslen, &errcode);
        if (errcode != 0) {
            handle_error(errcode, jsonrpc_err_message(errcode), flags);
        }
        if (G_VALUE_TYPE(gval) != CAM_DBUS_HASH_MAP) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        h = g_value_peek_pointer(gval);
    }
    /* Initialize the DBus system. */
    else {
        DBusGConnection* bus;
        DBusGProxy* proxy;
        gboolean okay;

        bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
        if (error != NULL) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        proxy = dbus_g_proxy_new_for_name(bus, service, path, iface);
        if (proxy == NULL) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal Error", flags);
        }
        if (params) {
            okay = dbus_g_proxy_call(proxy, method, &error,
                    G_VALUE_TYPE(params), g_value_peek_pointer(params), G_TYPE_INVALID,
                    CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
        } else {
            okay = dbus_g_proxy_call(proxy, method, &error, G_TYPE_INVALID,
                    CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
        }
        if (!okay) {
            handle_error(error->code, error->message, flags);
        }
    }

    /* Output CGI header data */
    if (flags & OPT_FLAG_CGI) {
        fprintf(stdout, "Content-type: application/json\n");
        fprintf(stdout, "\n");
    }

    /* Output format: Naked JSON */
    json_printf_dict(stdout, h, 0);
    fputc('\n', stdout);
    g_hash_table_destroy(h);
}
