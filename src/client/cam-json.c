#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "jsmn.h"
#include "api/cam-rpc.h"

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
json_printf_indent(FILE *fp, unsigned int depth, unsigned int comma)
{
    fprintf(fp, comma ? ",\n%*.s" : "\n%*.s", depth * JSON_TAB_SIZE, "");
    return 0;
}

void
json_printf_dict(FILE *fp, GHashTable *h, unsigned int depth)
{
    GHashTableIter i;
    gpointer key, value;
    unsigned int count = 0;

    fputs("{", fp);
    g_hash_table_iter_init(&i, h);
    while (g_hash_table_iter_next(&i, &key, &value)) {
        /* Append the comma and intent the next line as necessary. */
        json_printf_indent(fp, depth+1, count++);

        /* Print out the "name": value pair in JSON form. */
        json_printf_utf8(fp, key);
        fputs(": ", fp);
        json_printf_gval(fp, value);
    }
    if (count) json_printf_indent(fp, depth, 0);
    fputs("}", fp);
}

#define OPT_FLAG_RPC    (1<<0)
#define OPT_FLAG_CGI    (1<<1)

/* Standard JSON-RPC Error Codes. */
#define JSONRPC_ERR_PARSE_ERROR         (-32700)
#define JSONRPC_ERR_INVALID_REQUEST     (-32600)
#define JSONRPC_ERR_METHOD_NOT_FOUND    (-32601)
#define JSONRPC_ERR_INVALID_PARAMETERS  (-32602)
#define JSONRPC_ERR_INTERNAL_ERROR      (-32603)
#define JSONRPC_ERR_SERVER_ERROR        (-32604)

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
    else if (flags & OPT_FLAG_CGI) {
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

/* Put some upper bounds on the amount of JSON we're willing to parse. */
#define JSON_MAX_BUF    4096
#define JSON_MAX_TOK    128

/* Recursive helper to get the real size, in tokens, of a non-trivial token. */
static inline int
json_token_size(jsmntok_t *start)
{
    /* Recursively count objects. */
    if (start->type == JSMN_OBJECT) {
        int i;
        jsmntok_t *tok = start+1;
        for (i = 0; i < start->size; i++) {
            tok++;
            tok += json_token_size(tok);
        }
        return tok - start;
    }
    /* Recursively count arrays. */
    if (start->type == JSMN_ARRAY) {
        int i;
        jsmntok_t *tok = start+1;
        for (i = 0; i < start->size; i++) {
            tok += json_token_size(tok);
        }
        return tok - start;
    }
    /* Everything else has size 1. */
    return 1;
}

/* Parse JSON args, or return NULL if no args were provided. */
static GHashTable *
json_parse(FILE *fp, unsigned long flags)
{
    jsmn_parser parser;
    jsmntok_t tokens[JSON_MAX_TOK]; /* Should be enough for any crazy API thing. */
    char    *js = malloc(JSON_MAX_BUF);
    size_t  jslen;
    int     num;

    /* Read the file for JSON data. */
    jslen = fread(js, 1, JSON_MAX_BUF, fp);
    if (!jslen) {
        /* If we just get an EOF then assume no args. */
        if (feof(fp)) return NULL;
        /* Otherwise, report a parsing error. */
        handle_error(JSONRPC_ERR_PARSE_ERROR, "Invalid JSON", flags);
    }

    /* If there is more input, then our buffer is too small. */
    if (!feof(fp)) {
        /* We really want a 'request too large' error for CGI mode. */
        handle_error(JSONRPC_ERR_PARSE_ERROR, "Invalid JSON", flags);
    }

    jsmn_init(&parser);
    num = jsmn_parse(&parser, js, jslen, tokens, JSON_MAX_TOK);
    if (num < 0) {
        /* We really want a 'request too large' error for CGI mode. */
        handle_error(JSONRPC_ERR_PARSE_ERROR, "Invalid JSON", flags);
    }

    /* The only encoding we support for now is the JSON object */
    if (tokens[0].type == JSMN_OBJECT) {
        int children, i = 1;
        GHashTable *h = cam_dbus_dict_new();
        if (!h) {
            handle_error(JSONRPC_ERR_INTERNAL_ERROR, "Internal error", flags);
        }

        /* Parse the tokens making up this JSON object. */
        for (children = 0; children < tokens[0].size; children++) {
            jsmntok_t   *tok;
            char        *name;
            char        *value;

            /* To be a valid JSON object, the first token must be a string. */
            if (tokens[i].type != JSMN_STRING) {
                handle_error(JSONRPC_ERR_PARSE_ERROR, "Invalid JSON", flags);
            }
            name = &js[tokens[i].start];
            name[tokens[i].end - tokens[i].start] = '\0';
            i++;

            /* The following token can be any simple type. */
            tok = &tokens[i];
            value = &js[tok->start];
            value[tok->end - tok->start] = '\0';
            i += json_token_size(tok);

            if (tok->type == JSMN_STRING) {
                /* TODO: Deal with escaped UTF-8. This is probably a security hole. */
                cam_dbus_dict_add_printf(h, name, "%s", value);
            }
            else if (tok->type != JSMN_PRIMITIVE) {
                /* Ignore nested types. */
                continue;
            }
            /* Break it down by primitive types. */
            if (*value == 't') {
                cam_dbus_dict_add_boolean(h, name, 1);
            } else if (*value == 'f') {
                cam_dbus_dict_add_boolean(h, name, 0);
            } else if (*value == 'n') {
                /* TODO: Explicit NULL types and other magic voodoo? */
            }
            /* Below here, we have some kind of numeric type. */
            else if (strcspn(value, ".eE") != (tok->end - tok->start)) {
                char *e;
                double x = strtod(value, &e);
                if (*e != 0) continue; /* TODO: throw an "invalid JSON"? */
                /* TODO: DBUS encoding for doubles? */
            } else if (*value == '-') {
                /* Use the type 'long long' for negative values. */
                char *e;
                long long x = strtoll(value, &e, 0);
                if (*e != 0) continue; /* TODO: throw an "invalid JSON"? */
                cam_dbus_dict_add_int(h, name, x);
            } else {
                char *e;
                unsigned long long x = strtoull(value, &e, 0);
                if (*e != 0) continue; /* TODO: throw an "invalid JSON"? */
                cam_dbus_dict_add_uint(h, name, x);
            }
        }

        /* Success */
        free(js);
        return h;
    }

    /* Not any kind of input that we can make sense of. */
    free(js);
    return NULL;
}

static void
usage(FILE *fp, int argc, char * const argv[])
{
    fprintf(fp, "usage : %s [options] METHOD [PARAMS]\n\n", argv[0]);

    fprintf(fp, "Make a DBus call to the Chronos camera daemon, and translate\n");
    fprintf(fp, "the result into JSON. Parameters passed to the RPC call will\n");
    fprintf(fp, "be parsed from the PARAMS file, if provided.\n\n");

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-r, --rpc     encode the results in JSON-RPC format\n");
    fprintf(fp, "\t-c, --cgi     encode the results in CGI/1.0 format\n");
    fprintf(fp, "\t-n, --control connect to the control DBus interface\n");
    fprintf(fp, "\t-v, --video   connect to the video DBus interface\n");
    fprintf(fp, "\t-h, --help    display this help and exit\n");
}

int
main(int argc, char * const argv[])
{
    DBusGConnection* bus;
    DBusGProxy* proxy;
    GHashTable *h;
    GHashTable *params = NULL;
    GError* error = NULL;
    gboolean okay;
    const char *service = CAM_DBUS_CONTROL_SERVICE;
    const char *path = CAM_DBUS_CONTROL_PATH;
    const char *iface = CAM_DBUS_CONTROL_INTERFACE;
    const char *method;
    unsigned long flags = 0;
    
    /* Option Parsing */
    const char *short_options = "rvnch";
    const struct option long_options[] = {
        {"rpc",     no_argument,    0, 'r'},
        {"cgi",     no_argument,    0, 'c'},
        {"video",   no_argument,    0, 'v'},
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
            
            case 'c':
                flags |= OPT_FLAG_CGI;
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

    /* If CGI, get the requested method from the PATH_INFO variable. */
    if (flags & OPT_FLAG_CGI) {
        method = getenv("PATH_INFO");
        if (!method) {
            fprintf(stderr, "Missing variable: PATH_INFO\n");
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return EXIT_FAILURE;
        }
        /* Stip leading slashes from PATH_INFO */
        while (*method == '/') method++;
    }
    /* Otherwise, the method name is passed in via the command line. */
    else if (optind >= argc) {
        fprintf(stderr, "Missing argument: METHOD\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }
    else {
        method = argv[optind++];
    }

    /* If yet another parameter is present, it may provide a source file for
     * the RPC request parameters, or it may be '-' to read paramers from stdin.
     */
    if (optind < argc) {
        const char *filename = argv[optind++];
        FILE *fp = strcmp(filename, "-") ? fopen(filename, "r") : stdin;
        if (!fp) {
            fprintf(stderr, "Failed to open '%s' for reading: %s\n", filename, strerror(errno));
            return EXIT_FAILURE;
        }
        params = json_parse(fp, flags);
        fclose(fp);
    }
    
    /* Initialize the DBus system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        handle_error(-32603, "Internal error", flags);
    }
    proxy = dbus_g_proxy_new_for_name(bus, service, path, iface);
    if (proxy == NULL) {
        handle_error(-32603, "Internal error", flags);
    }
    if (params) {
        okay = dbus_g_proxy_call(proxy, method, &error,
                CAM_DBUS_HASH_MAP, params, G_TYPE_INVALID,
                CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
    } else {
        okay = dbus_g_proxy_call(proxy, method, &error, G_TYPE_INVALID,
                CAM_DBUS_HASH_MAP, &h, G_TYPE_INVALID);
    }
    if (!okay) {
        handle_error(error->code, error->message, flags);
    }

    /* Output CGI header data */
    if (flags & OPT_FLAG_CGI) {
        fprintf(stdout, "Content-type: application/json\n");
        fprintf(stdout, "\n");
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
