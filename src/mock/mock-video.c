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
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "mock.h"
#include "api/cam-rpc.h"

/*-------------------------------------
 * DBUS API Calls
 *-------------------------------------
 */
gboolean
cam_video_record_file(MockVideo *mock, GHashTable *args, GError **error)
{
#if 0
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_string(dict, "model", "Mock Camera 1.4");
        cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
        cam_dbus_dict_add_printf(dict, "fpgaVersion", "%d.%d", 3, 14);
        cam_dbus_dict_add_printf(dict, "memoryGB", "%u", 8);
        cam_dbus_dict_add_string(dict, "serial", "e^(i*pi)");
    }
    *data = dict;
    return (dict != NULL);
#else
    return 1;
#endif
}

gboolean
cam_video_livestream(MockVideo *mock, GHashTable *args, GError **error)
{
#if 0
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_string(dict, "model", "Mock Camera 1.4");
        cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
        cam_dbus_dict_add_printf(dict, "fpgaVersion", "%d.%d", 3, 14);
        cam_dbus_dict_add_printf(dict, "memoryGB", "%u", 8);
        cam_dbus_dict_add_string(dict, "serial", "e^(i*pi)");
    }
    *data = dict;
    return (dict != NULL);
#else
    return 1;
#endif
}

gboolean
cam_video_setframe(MockVideo *mock, GHashTable *args, GError **error)
{
#if 0
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_string(dict, "model", "Mock Camera 1.4");
        cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
        cam_dbus_dict_add_printf(dict, "fpgaVersion", "%d.%d", 3, 14);
        cam_dbus_dict_add_printf(dict, "memoryGB", "%u", 8);
        cam_dbus_dict_add_string(dict, "serial", "e^(i*pi)");
    }
    *data = dict;
    return (dict != NULL);
#else
    return 1;
#endif
}

gboolean
cam_video_playrate(MockVideo *mock, GHashTable *args, GError **error)
{
#if 0
    GHashTable *dict = cam_dbus_dict_new();
    /* TODO: Implement Real Stuff */
    if (dict) {
        cam_dbus_dict_add_uint(dict, "hRes", mock->hres);
        cam_dbus_dict_add_uint(dict, "vRes", mock->vres);
        cam_dbus_dict_add_uint(dict, "hOffset", mock->hoff);
        cam_dbus_dict_add_uint(dict, "vOffset", mock->voff);
        cam_dbus_dict_add_uint(dict, "exposureNsec", mock->exposure_nsec);
        cam_dbus_dict_add_uint(dict, "periodNsec", mock->period_nsec);
        cam_dbus_dict_add_uint(dict, "gain", mock->gain_db);
    }
    *data = dict;
    return (dict != NULL);
#else
    return 1;
#endif
}
