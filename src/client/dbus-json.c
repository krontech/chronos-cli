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

#include "api/cam-rpc.h"
#include "dbus-json.h"

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
        GType subtype = dbus_g_type_get_collection_specialization(G_VALUE_TYPE(val));
        json_printf_array(fp, g_value_peek_pointer(val), subtype, depth+1);
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
        case G_TYPE_STRING:
            /* TODO: Implement Me! */
        default:
            /* Default unknown types to null */
            fputs("null", fp);
            break;
    }
}

void
json_printf_array(FILE *fp, GArray *array, GType subtype, unsigned int depth)
{
    guint i;

    fputs("[", fp);
    for (i = 0; i < array->len; i++) {
        json_printf_indent(fp, depth+1, (i != 0));
        json_printf_arrayval(fp, array, subtype, i);
    }
    if (array->len) json_printf_indent(fp, depth, 0);
    fputs("]", fp);
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
