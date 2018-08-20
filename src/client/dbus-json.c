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
    } else if (G_VALUE_HOLDS_FLOAT(val)) {
        fprintf(fp, "%g", (double)g_value_get_float(val));
    } else if (G_VALUE_HOLDS_DOUBLE(val)) {
        fprintf(fp, "%g", g_value_get_double(val));
    } else {
        fputs("null", fp);
    }
    /* TODO: Still need 64-bit support */
    /* TODO: The magical wonderland of recursion awaits. */
}

static int
json_printf_indent(FILE *fp, unsigned int depth, unsigned int comma)
{
    fprintf(fp, comma ? ",%s%*.s" : "%s%*.s", json_newline, depth * JSON_TAB_SIZE, "");
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
