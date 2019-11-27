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
#ifndef __DBUS_JSON_H
#define __DBUS_JSON_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#define JSON_TAB_SIZE   3

extern const char *json_newline;

void json_printf_utf8(FILE *fp, const gchar *p);
void json_printf_array(FILE *fp, const GValue *gval, unsigned int depth);
void json_printf_dict(FILE *fp, GHashTable *h, unsigned int depth);

/* Standard JSON-RPC Error Codes. */
#define JSONRPC_ERR_PARSE_ERROR         (-32700)
#define JSONRPC_ERR_INVALID_REQUEST     (-32600)
#define JSONRPC_ERR_METHOD_NOT_FOUND    (-32601)
#define JSONRPC_ERR_INVALID_PARAMETERS  (-32602)
#define JSONRPC_ERR_INTERNAL_ERROR      (-32603)
#define JSONRPC_ERR_SERVER_ERROR        (-32604)

GValue *json_parse(const char *js, size_t jslen, int *errcode);
GValue *json_parse_file(FILE *fp, int *errcode);

#endif /* __DBUS_JSON_H */
