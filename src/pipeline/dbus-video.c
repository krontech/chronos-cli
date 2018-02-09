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

static GHashTable *
cam_dbus_video_status(struct pipeline_state *state)
{
    GHashTable *dict = cam_dbus_dict_new();
    if (!dict) return NULL;
    cam_dbus_dict_add_string(dict, "apiVersion", "1.0");
    cam_dbus_dict_add_boolean(dict, "playback", 0);     /* TODO: Pipeline state. */
    cam_dbus_dict_add_boolean(dict, "recording", 0);    /* TODO: Video record API */
    cam_dbus_dict_add_uint(dict, "segment", 0);

    cam_dbus_dict_add_uint(dict, "totalFrames", state->totalframes);
    cam_dbus_dict_add_uint(dict, "position", state->lastframe);
    cam_dbus_dict_add_int(dict, "framerate", state->playrate);
    return dict;
}

/*-------------------------------------
 * DBUS Video Control API
 *-------------------------------------
 */
typedef struct CamVideo {
    GObjectClass parent;
    struct pipeline_state *state;
} CamVideo;

typedef struct CamVideoClass {
    GObjectClass parent;
    guint eof_signalid;
} CamVideoClass;


static gboolean
cam_video_livestream(CamVideo *vobj, GHashTable *args, GError **error)
{
    return 1;
}

static gboolean
cam_video_addregion(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    unsigned long size = cam_dbus_dict_get_uint(args, "size", 0);
    unsigned long base = cam_dbus_dict_get_uint(args, "base", 0);
    unsigned long offset = cam_dbus_dict_get_uint(args, "offset", 0);
    GHashTable *dict = cam_dbus_dict_new();

    if (playback_region_add(state, base, size, offset) != 0) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "%s", strerror(errno));
        return 0;
    }

    *data = cam_dbus_dict_new();
    return (*data != NULL);
}

static gboolean
cam_video_status(CamVideo *vobj, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean cam_video_playback(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    struct pipeline_state *state = vobj->state;
    unsigned long framenum = cam_dbus_dict_get_uint(args, "position", state->lastframe);
    int framerate = cam_dbus_dict_get_int(args, "framerate", state->playrate);

    playback_set(state, framenum, framerate);
    *data = cam_dbus_video_status(state);
    return (data != NULL);
}

static gboolean
cam_video_recordfile(CamVideo *vobj, GHashTable *args, GHashTable **data, GError **error)
{
    return 1;
}

#include "api/cam-dbus-video.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */

GType cam_video_get_type(void);

#define CAM_VIDEO_TYPE              (cam_video_get_type())
#define CAM_VIDEO_GET_CLASS(_vobj_) (G_TYPE_INSTANCE_GET_CLASS((_vobj_), CAM_VIDEO_TYPE, CamVideoClass))

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

    /* Register signals. */
    vclass->eof_signalid = g_signal_new("eof", G_OBJECT_CLASS_TYPE(vclass),
                   G_SIGNAL_RUN_LAST,   /* How and when to run the signal. */
                   0,
                   NULL,                /* GSignalAccumulator to use. We don't need one. */
                   NULL,                /* User-data to pass to the accumulator. */
                   /* Function to use to marshal the signal data into
                      the parameters of the signal call. Luckily for
                      us, GLib (GCClosure) already defines just the
                      function that we want for a signal handler that
                      we don't expect any return values (void) and
                      one that will accept one string as parameter
                      (besides the instance pointer and pointer to
                      user-data).

                      If no such function would exist, you would need
                      to create a new one (by using glib-genmarshal
                      tool). */
                   g_cclosure_marshal_VOID__STRING,
                   G_TYPE_NONE,         /* Return GType of the return value. */
                   1,                   /* Number of parameter GTypes to follow. */
                   CAM_DBUS_HASH_MAP);  /* GType of the parameters. */

    dbus_g_object_type_install_info(CAM_VIDEO_TYPE, &dbus_glib_cam_video_object_info);
}

G_DEFINE_TYPE(CamVideo, cam_video, G_TYPE_OBJECT)

void
dbus_service_launch(struct pipeline_state *state)
{
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;
    GError* error = NULL;
    guint result;

    /* Init glib */
    g_type_init();
    state->video = g_object_new(CAM_VIDEO_TYPE, NULL);

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
    dbus_g_connection_register_g_object(bus, CAM_DBUS_VIDEO_PATH, G_OBJECT(state->video));
    printf("Registered video control device at %s\n", CAM_DBUS_VIDEO_PATH);
}

void
dbus_signal_eof(struct pipeline_state *state)
{
    CamVideoClass *vclass = CAM_VIDEO_GET_CLASS(state->video);
    g_signal_emit(state->video, vclass->eof_signalid, 0, cam_dbus_video_status(state));
}
