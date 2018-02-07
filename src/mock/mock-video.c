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
#include <sys/time.h>
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
cam_video_addregion(MockVideo *mock, GHashTable *args, GHashTable **data, GError **error)
{
    struct mock_state *state = mock->state;
    GHashTable *dict = cam_dbus_dict_new();

    state->region_size = cam_dbus_dict_get_uint(args, "size", 0);
    state->region_base = cam_dbus_dict_get_uint(args, "base", 0);
    state->region_offset = cam_dbus_dict_get_uint(args, "offset", 0);

    *data = cam_dbus_dict_new();
    return (*data != NULL);
}

#define MOCK_RECORDED_FRAMES    1234

static unsigned long
compute_frameno(struct mock_state *state)
{
    long long frame;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    frame = state->play_start_frame;
    frame += (now.tv_sec - state->play_start_time.tv_sec) * state->play_frame_rate;
    frame += (now.tv_nsec * state->play_frame_rate) / 1000000000;
    frame -= (state->play_start_time.tv_nsec * state->play_frame_rate) / 1000000000;
    frame %= MOCK_RECORDED_FRAMES;
    if (frame < 0) frame += MOCK_RECORDED_FRAMES;
    return frame;
}

gboolean
cam_video_playback(MockVideo *mock, GHashTable *args, GHashTable **data, GError **error)
{
    struct mock_state *state = mock->state;
    unsigned long position = cam_dbus_dict_get_uint(args, "position", compute_frameno(state));
    int framerate = cam_dbus_dict_get_int(args, "framerate", state->play_frame_rate);

    /* Store the updated playback timing. */
    state->play_frame_rate = framerate;
    state->play_start_frame = position;
    clock_gettime(CLOCK_MONOTONIC, &state->play_start_time);

    /* Return the updated playback status */
    return cam_video_status(mock, data, error);
}

gboolean
cam_video_status(MockVideo *mock, GHashTable **data, GError **error)
{
    struct mock_state *state = mock->state;
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
        cam_dbus_dict_add_boolean(dict, "playback", (state->play_start_time.tv_sec + state->play_start_time.tv_nsec) != 0);
        cam_dbus_dict_add_boolean(dict, "recording", 0); /* TODO: Video record API */
        cam_dbus_dict_add_uint(dict, "segment", 0);

        cam_dbus_dict_add_uint(dict, "totalFrames", MOCK_RECORDED_FRAMES);
        cam_dbus_dict_add_uint(dict, "positon", compute_frameno(state));
        cam_dbus_dict_add_int(dict, "framerate", state->play_frame_rate);
    }
    *data = dict;
    return (dict != NULL);
}
