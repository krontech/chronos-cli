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
#ifndef _MOCK_H
#define _MOCK_H

#include "fpga-sensor.h"

/* Don't yet have a good abstraction for this. */
#define MOCK_MAX_GAIN           24

struct mock_state {
    /* Video Settings */
    unsigned long hres;
    unsigned long vres;
    unsigned long hoff;
    unsigned long voff;
    unsigned long long exposure_nsec;
    unsigned long long period_nsec;
    int gain_db;

    struct image_sensor sensor;
};

/* DBus/Glib glue object. */
typedef struct {
    GObject parent;
    struct  mock_state *state;
} MockControl;

typedef struct {
    GObject parent;
    struct  mock_state *state;
} MockVideo;

/* Control API calls to be mocked. */
gboolean cam_control_get_video_settings(MockControl *cam, GHashTable **data, GError **error);
gboolean cam_control_set_video_settings(MockControl *cam, GHashTable *data, GError **error);
gboolean cam_control_get_camera_data(MockControl *cam, GHashTable **data, GError **error);
gboolean cam_control_get_sensor_data(MockControl *cam, GHashTable **data, GError **error);
gboolean cam_control_get_timing_limits(MockControl *cam, GHashTable *args, GHashTable **data, GError **error);

/* Video API calls to be mocked. */
gboolean cam_video_record_file(MockVideo *cam, GHashTable *args, GError **error);
gboolean cam_video_livestream(MockVideo *cam, GHashTable *args, GError **error);
gboolean cam_video_playrate(MockVideo *cam, GHashTable *args, GError **error);
gboolean cam_video_getstate(MockVideo *cam, GHashTable **resp, GError **error);
gboolean cam_video_setframe(MockVideo *cam, GHashTable *args, GError **error);

void mock_sensor_init(struct image_sensor *sensor);

#endif /* _MOCK_H */
