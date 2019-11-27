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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "jsmn.h"
#include "api/cam-rpc.h"
#include "dbus-json.h"

/*===============================================
 * DBus to JSON output conversion
 *===============================================
 */

/* Allow overloading the newline */
const char *json_newline = "\n";

/* Output a UTF-8 string, with the necessary escaping for JSON. */
void
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

static int
json_printf_indent(FILE *fp, unsigned int depth, unsigned int comma)
{
    fprintf(fp, comma ? ",%s%*.s" : "%s%*.s", json_newline, depth * JSON_TAB_SIZE, "");
    return 0;
}

static void
json_printf_gval(FILE *fp, gconstpointer val, unsigned int depth)
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
    } else if (G_VALUE_HOLDS_FLOAT(val)) {
        fprintf(fp, "%g", (double)g_value_get_float(val));
    } else if (G_VALUE_HOLDS_DOUBLE(val)) {
        fprintf(fp, "%g", g_value_get_double(val));
    } else if (G_VALUE_TYPE(val) == CAM_DBUS_HASH_MAP) {
        /* Recursively print nested dictionaries. */
        json_printf_dict(fp, g_value_peek_pointer(val), depth+1);
    } else if (dbus_g_type_is_collection(G_VALUE_TYPE(val))) {
        /* We got an array type. */
        json_printf_array(fp, val, depth+1);
    } else {
        /* Default unknown types to null */
        fprintf(stderr, "Unknown type found: %s\n", g_type_name(G_VALUE_TYPE(val)));
        fputs("null", fp);
    }
}

static void
json_printf_arrayval(FILE *fp, GArray *array, GType subtype, guint idx)
{
    switch (subtype) {
        case G_TYPE_INT:
            fprintf(fp, "%d", g_array_index(array, gint, idx));
            break;
        case G_TYPE_UINT:
            fprintf(fp, "%d", g_array_index(array, guint, idx));
            break;
        case G_TYPE_LONG:
            fprintf(fp, "%ld", g_array_index(array, glong, idx));
            break;
        case G_TYPE_ULONG:
            fprintf(fp, "%ld", g_array_index(array, gulong, idx));
            break;
        case G_TYPE_BOOLEAN:
            fputs(g_array_index(array, gboolean, idx) ? "true" : "false", fp);
            break;
        case G_TYPE_FLOAT:
            fprintf(fp, "%g", (double)g_array_index(array, gfloat, idx));
            break;
        case G_TYPE_DOUBLE:
            fprintf(fp, "%g", g_array_index(array, gdouble, idx));
            break;
        default:
            /* Default unknown types to null */
            fputs("null", fp);
            break;
    }
}

void
json_printf_array(FILE *fp, const GValue *gval, unsigned int depth)
{
    const gchar *typename = g_type_name(G_VALUE_TYPE(gval));
    GType subtype = dbus_g_type_get_collection_specialization(G_VALUE_TYPE(gval));
    guint i;

    /* Complex types encoded as a GPtrArray. */
    if (g_str_has_prefix(typename, "GPtrArray")) {
        GPtrArray *array = g_value_peek_pointer(gval);
        fputs("[", fp);
        for (i = 0; i < array->len; i++) {
            GValue gval;
            
            memset(&gval, 0, sizeof(gval));
            g_value_init(&gval, subtype);
            g_value_set_boxed(&gval, g_ptr_array_index(array, i));

            json_printf_indent(fp, depth+1, (i != 0));
            json_printf_gval(fp, &gval, depth);
        }
        if (array->len) json_printf_indent(fp, depth, 0);
        fputs("]", fp);
    }
    /* Simple types encoded as a GArray */
    else {
        GArray *array = g_value_peek_pointer(gval);
        fputs("[", fp);
        for (i = 0; i < array->len; i++) {
            json_printf_indent(fp, depth+1, (i != 0));
            json_printf_arrayval(fp, array, subtype, i);
        }
        if (array->len) json_printf_indent(fp, depth, 0);
        fputs("]", fp);
    }
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
        json_printf_gval(fp, value, depth);
    }
    if (count) json_printf_indent(fp, depth, 0);
    fputs("}", fp);
}

/*===============================================
 * JSON to DBus input conversion
 *===============================================
 */

static GValue *json_parse_token(jsmntok_t *tok, const char *js, int *errcode);

/* Put some upper bounds on the amount of JSON we're willing to parse. */
#define JSON_MAX_BUF    32768

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

/* Parse a simple JSON array where all members are of the same type. */
static GValue *
json_parse_array(jsmntok_t *tokens, const char *js, int *errcode)
{
    int children, i = 1;
    GValue *gval = g_new0(GValue, 1);
    if (!gval) {
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        return NULL;
    }

    /* String cases - use a GPtrArray */
    if (tokens[1].type == JSMN_STRING) {
        GPtrArray *arr = g_ptr_array_sized_new(tokens->size);
        if (!arr) {
            *errcode = JSONRPC_ERR_INTERNAL_ERROR;
            g_free(gval);
            return NULL;
        }
        g_ptr_array_set_free_func(arr, g_free);

        /* Add all strings to the pointer array.  */
        for (children = 0; children < tokens->size; children++) {
            jsmntok_t *tok = &tokens[i];
            if (tok->type == JSMN_STRING) {
                const char *value = &js[tok->start];
                g_ptr_array_add(arr, g_strndup(value, tok->end - tok->start));
            }
            i += json_token_size(tok);
        }
        g_value_init(gval, dbus_g_type_get_collection("GPtrArray", G_TYPE_STRING));
        g_value_take_boxed(gval, arr);
        return gval;
    }
    else if (tokens[1].type != JSMN_PRIMITIVE) {
        /* Arrays of complex types are not supported for now. */
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        g_free(gval);
        return NULL;
    }
    /* If the first token is 'true', 'false', or 'null' then build an array of booleans. */
    else if (memchr("tfn", js[tokens[1].start], 3) != NULL) {
        GArray *arr = g_array_sized_new(FALSE, TRUE, sizeof(gboolean), tokens->size);
        if (!arr) {
            *errcode = JSONRPC_ERR_INTERNAL_ERROR;
            g_free(gval);
            return NULL;
        }
        g_array_set_size(arr, tokens->size);
        
        /* Add all doubles to the pointer array. */
        for (children = 0; children < tokens->size; children++) {
            jsmntok_t *tok = &tokens[i];
            if (tok->type == JSMN_PRIMITIVE) {
                g_array_index(arr, gboolean, children) = (js[tok->start] == 't');
            }
            i += json_token_size(tok);
        }
        g_value_init(gval, dbus_g_type_get_collection("GArray", G_TYPE_BOOLEAN));
        g_value_take_boxed(gval, arr);
        return gval;
    }
    /*
     * Otherwise, the array could contain integers or floats, choose
     * the lowest common denominator and build an array of doubles.
     */
    else {
        GArray *arr = g_array_sized_new(FALSE, TRUE, sizeof(gdouble), tokens->size);
        if (!arr) {
            *errcode = JSONRPC_ERR_INTERNAL_ERROR;
            g_free(gval);
            return NULL;
        }
        g_array_set_size(arr, tokens->size);

        /* Add all doubles to the pointer array. */
        for (children = 0; children < tokens->size; children++) {
            jsmntok_t *tok = &tokens[i];
            if (tok->type == JSMN_PRIMITIVE) {
                char *endp;
                double value = strtod(&js[tok->start], &endp);
                if (endp == &js[tok->end]) {
                    g_array_index(arr, gdouble, children) = value;
                }
            }
            i += json_token_size(tok);
        }
        g_value_init(gval, dbus_g_type_get_collection("GArray", G_TYPE_DOUBLE));
        g_value_take_boxed(gval, arr);
        return gval;
    }
}

static GValue *
json_parse_object(jsmntok_t *tokens, const char *js, int *errcode)
{
    int children, i = 1;
    GHashTable *hash = cam_dbus_dict_new();
    GValue *gval;
    if (!hash) {
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        return NULL;
    }

    /* Parse the tokens making up this JSON object. */
    for (children = 0; children < tokens[0].size; children++) {
        const char  *name;
        size_t      nlength;
        GValue      *value;

        /* To be a valid JSON object, the first token must be a string. */
        if (tokens[i].type != JSMN_STRING) {
            cam_dbus_dict_free(hash);
            *errcode = JSONRPC_ERR_PARSE_ERROR;
            return NULL;
        }
        nlength = tokens[i].end - tokens[i].start;
        name = &js[tokens[i].start];
        i++;

        /* Parse the token value into a GValue */
        value = json_parse_token(&tokens[i], js, errcode);
        if (!value) {
            cam_dbus_dict_free(hash);
            return NULL;
        }
        g_hash_table_insert(hash, g_strndup(name, nlength), value);
        i += json_token_size(&tokens[i]);
    }

    /* Allocate a GValue for the result. */
    if ((gval = g_new0(GValue, 1)) == NULL) {
        cam_dbus_dict_free(hash);
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        return NULL;
    }

    /* Success */
    g_value_init(gval, CAM_DBUS_HASH_MAP);
    g_value_take_boxed(gval, hash);
    return gval;
}

static GValue *
json_parse_token(jsmntok_t *tok, const char *js, int *errcode)
{
    const char *value = &js[tok->start];
    size_t length = tok->end - tok->start;
    GValue *gval = NULL;

    /* Special cases. */
    if (tok->type == JSMN_STRING) {
        gchar *newstring = g_strndup(value, length);
        if (!newstring) {
            *errcode = JSONRPC_ERR_INTERNAL_ERROR;
            return NULL;
        }
        gval = g_new0(GValue, 1);
        if (!gval) {
            g_free(newstring);
            *errcode = JSONRPC_ERR_INTERNAL_ERROR;
            return NULL;
        }
        g_value_init(gval, G_TYPE_STRING);
        g_value_take_string(gval, newstring);
        return gval;
    }
    else if (tok->type == JSMN_OBJECT) {
        return json_parse_object(tok, js, errcode);
    }
    else if (tok->type == JSMN_ARRAY) {
        return json_parse_object(tok, js, errcode);
    }
    /* Otherwise, a primitive type is expected. */
    else if (tok->type != JSMN_PRIMITIVE) {
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        return NULL;
    }

    /* Allocate memory for the value */
    gval = g_new0(GValue, 1);
    if (!gval) {
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        return NULL;
    }

    /* Break it down by primitive types. */
    if (*value == 't') {
        g_value_init(gval, G_TYPE_BOOLEAN);
        g_value_set_boolean(gval, TRUE);
    } else if (*value == 'f') {
        g_value_init(gval, G_TYPE_BOOLEAN);
        g_value_set_boolean(gval, FALSE);
    } else if (*value == 'n') {
        /* D-Bus does not support an explicit null, so make it false intead. */
        g_value_init(gval, G_TYPE_BOOLEAN);
        g_value_set_boolean(gval, FALSE);
    }
    /* Below here, we have some kind of numeric type. */
    else if (strcspn(value, ".eE") != (tok->end - tok->start)) {
        char *end;
        double x = strtod(value, &end);
        g_value_init(gval, G_TYPE_DOUBLE);
        if (end == &js[tok->end]) g_value_set_double(gval, x);
    } else if (*value == '-') {
        /* Use the type 'long long' for negative values. */
        char *end;
        long long x = strtoll(value, &end, 0);
        g_value_init(gval, G_TYPE_INT);
        if (end == &js[tok->end]) g_value_set_int(gval, x);
    } else {
        char *end;
        unsigned long long x = strtoull(value, &end, 0);
        g_value_init(gval, G_TYPE_UINT);
        if (end == &js[tok->end]) g_value_set_uint(gval, x);
    }

    return gval;
}

/* Parse JSON text and return the object or array as D-Bus types */
GValue *
json_parse(const char *js, size_t jslen, int *errcode)
{
    jsmn_parser parser;
    jsmntok_t *tokens = NULL;
    GValue  *gval;
    int     num;
    int     ret;

    /* Return no-error when appropriate. */
    if (!errcode) errcode = &ret;
    *errcode = 0;

    /* Allocate memory for the JSON tokens. */
    jsmn_init(&parser);
    num = jsmn_parse(&parser, js, jslen, NULL, 0);
    if (num <= 0) {
        /* We got an invalid blob of JSON that could not be parsed. */
        *errcode = JSONRPC_ERR_PARSE_ERROR;
        return NULL;
    }
    tokens = malloc(sizeof(jsmntok_t) * num);
    if (!tokens) {
        *errcode = JSONRPC_ERR_PARSE_ERROR;
        return NULL;
    }
    
    /* Parse the JSON into tokens. */
    jsmn_init(&parser);
    num = jsmn_parse(&parser, js, jslen, tokens, num);
    if (num < 0) {
        /* We really want a 'request too large' error for CGI mode. */
        *errcode = JSONRPC_ERR_PARSE_ERROR;
        return NULL;
    }

    /* Parse the tokens into a GValue */
    if (tokens[0].type == JSMN_OBJECT) {
        gval = json_parse_object(tokens, js, errcode);
    }
    else if (tokens[0].type == JSMN_ARRAY) {
        gval = json_parse_array(tokens, js, errcode);
    }
    else {
        /* Otherwise, we don't quite support this type. */
        *errcode = JSONRPC_ERR_PARSE_ERROR;
        gval = NULL;
    }

    free(tokens);
    return gval;
}

GValue *
json_parse_file(FILE *fp, int *errcode)
{
    char    *js = malloc(JSON_MAX_BUF);
    GValue  *gval = NULL;
    size_t  jslen;
    int     num;

    /* If allocation failed, then give up. */
    if (!js) {
        *errcode = JSONRPC_ERR_INTERNAL_ERROR;
        return NULL;
    }

    /* Read the file for JSON data. */
    jslen = fread(js, 1, JSON_MAX_BUF, fp);
    if (!jslen) {
        /* If we just get an EOF then assume no args. */
        if (!feof(fp)) *errcode = JSONRPC_ERR_PARSE_ERROR;
    }
    /* If there is more input, then our buffer is too small. */
    else if (!feof(fp)) {
        *errcode = JSONRPC_ERR_PARSE_ERROR;
    }
    /* Otherwise, parse JSON into a GValue. */
    else {
        gval = json_parse(js, jslen, errcode);
    }
    free(js);
    return gval;
}
