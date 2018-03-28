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
void json_printf_dict(FILE *fp, GHashTable *h, unsigned int depth);

#endif /* __DBUS_JSON_H */
