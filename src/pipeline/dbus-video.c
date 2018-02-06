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
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dbus/dbus-glib.h>

#include "pipeline.h"
#include "api/cam-rpc.h"

/*-------------------------------------
 * DBUS Video Control API
 *-------------------------------------
 */
typedef struct {
    GObjectClass parent;
    struct pipeline_state *state;
} CamVideo;

typedef struct {
    GObjectClass parent;
} CamVideoClass;

static gboolean
cam_video_record_file(CamVideo *vobj, GHashTable *args, GError **error)
{
    return 1;
}

static gboolean
cam_video_livestream(CamVideo *vobj, GHashTable *args, GError **error)
{
    return 1;
}

static gboolean
cam_video_addregion(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    GHashTable *dict = cam_dbus_dict_new();

    state->region_size = cam_dbus_dict_get_uint(args, "size", 0);
    state->region_base = cam_dbus_dict_get_uint(args, "base", 0);
    state->region_first = cam_dbus_dict_get_uint(args, "first", 0);

    /* Recompute the video display area */
    state->totalframes = state->region_size / state->fpga->seq->frame_size;

    *data = cam_dbus_dict_new();
    return (*data != NULL);
}

static gboolean
cam_video_status(CamVideo *vobj, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
        cam_dbus_dict_add_boolean(dict, "playback", 0);     /* TODO: Pipeline state. */
        cam_dbus_dict_add_boolean(dict, "recording", 0);    /* TODO: Video record API */
        cam_dbus_dict_add_uint(dict, "segment", 0);

        cam_dbus_dict_add_uint(dict, "totalFrames", state->totalframes);
        cam_dbus_dict_add_uint(dict, "currentFrame", state->lastframe);
        cam_dbus_dict_add_int(dict, "playbackRate", state->playrate);
    }
    *data = dict;
    return (dict != NULL);
}

static gboolean cam_video_playback(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    unsigned long framenum = cam_dbus_dict_get_uint(args, "currentFrame", state->lastframe);
    int framerate = cam_dbus_dict_get_int(args, "playbackRate", state->playrate);

    state->playrate = framerate;
    state->lastframe = framenum;

    /* Reschedule the playback timer. */
    if (framerate == 0) {
        /* Pause the video by playing the same frame at 60Hz */
        unsigned long nsec = 1000000000 / LIVE_MAX_FRAMERATE;
        struct itimerspec ts = {
            .it_interval = {0, nsec},
            .it_value = {0, nsec},
        };
        timer_settime(state->playtimer, 0, &ts, NULL);
        fprintf(stderr, "DEBUG: setting playback rate to 0 fps.\n");
    }
    /* Start the playback frame timer. */
    else {
        unsigned int divisor = (abs(framerate) + LIVE_MAX_FRAMERATE - 1) / LIVE_MAX_FRAMERATE;
        unsigned long nsec = (1000000000 + abs(framerate) - 1) / (abs(framerate) * divisor);
        struct itimerspec ts = {
            .it_interval = {0, nsec},
            .it_value = {0, nsec},
        };
        timer_settime(state->playtimer, 0, &ts, NULL);
        fprintf(stderr, "DEBUG: setting playback rate to %d/%u fps.\n", framerate, divisor);
    }

    /* Return the updated playback status */
    return cam_video_status(vobj, data, error);
}

#include "api/cam-dbus-video.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */

GType cam_video_get_type(void);

#define CAM_VIDEO_TYPE      (cam_video_get_type())

static void
cam_video_init(CamVideo *vobj)
{
    g_assert(vobj != NULL);
    vobj->state = NULL;
}

static void
cam_video_class_init(CamVideoClass *vclass)
{
    g_assert(vclass != NULL);
    dbus_g_object_type_install_info(CAM_VIDEO_TYPE, &dbus_glib_cam_video_object_info);
}

G_DEFINE_TYPE(CamVideo, cam_video, G_TYPE_OBJECT)

void
dbus_service_launch(struct pipeline_state *state)
{
    guint result;
    CamVideo *vobj;
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;
    GError* error = NULL;

    /* Init glib */
    g_type_init();
    vobj = g_object_new(CAM_VIDEO_TYPE, NULL);
    vobj->state = state;

    /* Bring up DBus and register with the system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }

    proxy = dbus_g_proxy_new_for_name(bus, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);
    if (proxy == NULL) {
        g_printerr("Failed to get a proxy for D-Bus (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (!dbus_g_proxy_call(proxy,
                            "RequestName",
                            &error,
                            G_TYPE_STRING, CAM_DBUS_VIDEO_SERVICE,
                            G_TYPE_UINT, 0,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &result,
                            G_TYPE_INVALID)) {
        g_printerr("D-Bus.RequstName RPC failed (%s)\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (result != 1) {
        g_printerr("D-Bus.RequstName call failed for %s\n", CAM_DBUS_VIDEO_SERVICE);
        exit(EXIT_FAILURE);
    }
    dbus_g_connection_register_g_object(bus, CAM_DBUS_VIDEO_PATH, G_OBJECT(vobj));
    printf("Registered video control device at %s\n", CAM_DBUS_VIDEO_PATH);
}