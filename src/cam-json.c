#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "api/cam-rpc.h"
#include "api/cam-dbus-client.h"

/* Output a UTF-8 string, with the necessary escaping for JSON. */
static void
json_printf_utf8(FILE *fp, const gchar *p)
{
    fputc('"', fp);
    while (*p) {
        gunichar c = g_utf8_get_char(p);
        gchar *next = g_utf8_next_char(p);
        if (c == '\\') {
            fputs("\\\\", fp);
        }
        else if (c == '"') {
            fputs("\\\"", fp);
        }
        else if (c < 0x20) {
            fprintf(fp, "\\u%04x", c);
        }
        else if (c > 0xff) {
            fwrite(p, (next - p), 1, fp);
        }
        else {
            fputc(c, fp);
        }
        p = next;
    }
    fputc('"', fp);
}

static void
json_printf_gval(FILE *fp, gconstpointer val)
{
    if (G_VALUE_HOLDS_STRING(val)) {
        json_printf_utf8(fp, g_value_get_string(val));
    } else if (G_VALUE_HOLDS_INT(val)) {
        fprintf(fp, "%d", g_value_get_int(val));
    } else if (G_VALUE_HOLDS_UINT(val)) {
        fprintf(fp, "%u", g_value_get_uint(val));
    } else if (G_VALUE_HOLDS_LONG(val)) {
        fprintf(fp, "%ld", g_value_get_long(val));
    } else if (G_VALUE_HOLDS_ULONG(val)) {
        fprintf(fp, "%lu", g_value_get_ulong(val));
    } else if (G_VALUE_HOLDS_BOOLEAN(val)) {
        fputs(g_value_get_boolean(val) ? "true" : "false", fp);
    } else {
        fputs("null", fp);
    }
    /* TODO: Still need floating point and 64-bit support */
    /* TODO: The magical wonderland of recursion awaits. */
}

#define JSON_TAB_SIZE   3
static int
json_printf_indent(FILE *fp, unsigned int depth, unsigned int nocomma)
{
    fprintf(fp, nocomma ? "\n%*.s" : ",\n%*.s", depth * JSON_TAB_SIZE, "");
    return 0;
}

void
json_printf_dict(FILE *fp, GHashTable *h, unsigned int depth)
{
    GHashTableIter i;
    gpointer key, value;
    unsigned int first = 1;

    fputs("{", fp);
    g_hash_table_iter_init(&i, h);
    while (g_hash_table_iter_next(&i, &key, &value)) {
        /* Append the comma and intent the next line as necessary. */
        first = json_printf_indent(fp, depth+1, first);

        /* Print out the "name": value pair in JSON form. */
        json_printf_utf8(fp, key);
        fputs(": ", fp);
        json_printf_gval(fp, value);
    }
    if (!first) json_printf_indent(fp, depth, 1);
    fputs("}", fp);
}

#define OPT_FLAG_RPC    (1<<0)

static void
handle_error(int code, const char *message, unsigned long flags)
{
    if (flags & OPT_FLAG_RPC) {
        fputs("{\n", stdout);
        fprintf(stdout, "%*.s\"jsonrpc\": \"2.0\",\n", JSON_TAB_SIZE, "");
        fprintf(stdout, "%*.s\"id\": null,\n", JSON_TAB_SIZE, "");
        fprintf(stdout, "%*.s\"error\": {\n", JSON_TAB_SIZE, "");
        fprintf(stdout, "%*.s\"code\": %d,\n", 2 * JSON_TAB_SIZE, "", code);
        fprintf(stdout, "%*.s\"message\": ", 2 * JSON_TAB_SIZE, "");
        json_printf_utf8(stdout, message);
        fprintf(stdout, "\n%*.s}\n", JSON_TAB_SIZE, "");
        fputs("}\n", stdout);
    }
    else {
        fprintf(stderr, "RPC Call Failed: %s\n", message);
    }
    exit(EXIT_FAILURE);
}

static void
usage(FILE *fp, int argc, char * const argv[])
{
    fprintf(fp, "usage : %s [options] METHOD\n\n", argv[0]);

    fprintf(fp, "Make a DBus call to the Chronos camera daemon, and translate\n");
    fprintf(fp, "the result into JSON.\n\n");

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-r, --rpc    encode the results in JSON-RPC format\n");
    fprintf(fp, "\t-h, --help   display this help and exit\n");
}

int
main(int argc, char * const argv[])
{
    DBusGConnection* bus;
    DBusGProxy* proxy;
    GHashTable *h;
    GError* error = NULL;
    const char *method;
    unsigned long flags = 0;
    
    /* Option Parsing */
    const char *short_options = "rh";
    const struct option long_options[] = {
        {"rpc",     no_argument,    0, 'r'},
        {"help",    no_argument,    0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 'r':
                flags |= OPT_FLAG_RPC;
                break;
            
            case 'h':
                usage(stdout, argc, argv);
                return EXIT_SUCCESS;
            case '?':
            default:
                return EXIT_FAILURE;
        }
    }
    /* Parse the method name. */
    if (optind >= argc) {
        fprintf(stderr, "Missing argument: METHOD\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }
    method = argv[optind];
    /* TODO: Parse JSON from stdin to get the request parameters, if any. */

    /* Initialize the GType/GObject system. */
    g_type_init();
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        handle_error(-32603, "Internal error", flags);
    }
    proxy = dbus_g_proxy_new_for_name(bus, CAM_DBUS_SERVICE, CAM_DBUS_PATH, CAM_DBUS_INTERFACE);
    if (proxy == NULL) {
        handle_error(-32603, "Internal error", flags);
    }
    if (!dbus_g_proxy_call(proxy, method, &error, G_TYPE_INVALID,
            CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID)) {
        handle_error(error->code, error->message, flags);
    }

    /* Output format: JSON-RPC */
    if (flags & OPT_FLAG_RPC) {
        fputs("{\n", stdout);
        fprintf(stdout, "%*.s\"jsonrpc\": \"2.0\",\n", JSON_TAB_SIZE, "");
        fprintf(stdout, "%*.s\"id\": null,\n", JSON_TAB_SIZE, "");
        fprintf(stdout, "%*.s\"result\": ", JSON_TAB_SIZE, "");
        json_printf_dict(stdout, h, 1);
        fputs("\n}\n", stdout);
    }
    /* Output format: Naked JSON */
    else {
        json_printf_dict(stdout, h, 0);
        fputc('\n', stdout);
    }
    g_hash_table_destroy(h);
}
