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

gboolean
cam_control_get_camera_data(MockControl *mock, GHashTable **data, GError **error)
{
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
}

gboolean
cam_control_get_video_settings(MockControl *mock, GHashTable **data, GError **error)
{
    struct mock_state *state = mock->state;
    GHashTable *dict = cam_dbus_dict_new();
    /* TODO: Implement Real Stuff */
    if (dict) {
        cam_dbus_dict_add_uint(dict, "hRes", state->hres);
        cam_dbus_dict_add_uint(dict, "vRes", state->vres);
        cam_dbus_dict_add_uint(dict, "hOffset", state->hoff);
        cam_dbus_dict_add_uint(dict, "vOffset", state->voff);
        cam_dbus_dict_add_uint(dict, "exposureNsec", state->exposure_nsec);
        cam_dbus_dict_add_uint(dict, "periodNsec", state->period_nsec);
        cam_dbus_dict_add_uint(dict, "gain", state->gain_db);
    }
    *data = dict;
    return (dict != NULL);
}

gboolean
cam_control_set_video_settings(MockControl *mock, GHashTable *data, GError **error)
{
    struct mock_state *state = mock->state;
    do {
        struct image_constraints constraints;
        struct image_geometry geometry = {
            .hres = cam_dbus_dict_get_uint(data, "hRes", 0),
            .vres = cam_dbus_dict_get_uint(data, "vRes", 0),
            .hoffset = cam_dbus_dict_get_uint(data, "hOffset", 0),
            .voffset = cam_dbus_dict_get_uint(data, "vOffset", 0),
        };
        unsigned int exp_nsec = cam_dbus_dict_get_uint(data, "exposureNsec", 0);
        unsigned int period_nsec = cam_dbus_dict_get_uint(data, "periodNsec", 0);
        int gain = cam_dbus_dict_get_int(data, "gain", 0);
        int err;

        /* Sanity check the hres and vres */
        err = image_sensor_set_resolution(&state->sensor, &geometry);
        if (err < 0) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid frame resolution and offset");
            break;
        }
        
        /* Sanity check the frame timing. */
        image_sensor_get_constraints(&state->sensor, &geometry, &constraints);
        if (period_nsec == 0) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Unspecified Frame Period");
            break;
        }
        if ((period_nsec < constraints.t_min_period) || (period_nsec > constraints.t_max_period)) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid Frame Period");
            break;
        }

        /* Sanity check the exposure time. */
        if (exp_nsec == 0) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Unspecified Exposure Time");
            break;
        }
        if (exp_nsec < constraints.t_min_exposure) exp_nsec = constraints.t_min_exposure;
        if (exp_nsec > image_sensor_max_exposure(&constraints, period_nsec)) {
            exp_nsec = image_sensor_max_exposure(&constraints, period_nsec);
        } else if (exp_nsec < constraints.t_min_exposure) {
            exp_nsec = constraints.t_min_exposure;
        }

        /* The gain defaults to zero if omitted, but otherwise must be a multiple of 6dB and less than MAX_GAIN */
        if ((gain % 6) || (gain > MOCK_MAX_GAIN) || (gain < 0)) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid Gain");
            break;
        }
        /* Store it */
        state->hres = geometry.hres;
        state->vres = geometry.vres;
        state->hoff = geometry.hoffset;
        state->voff = geometry.voffset;
        state->exposure_nsec = exp_nsec;
        state->period_nsec = period_nsec;
        state->gain_db = gain;

        g_hash_table_destroy(data);
        return TRUE;
    } while (0);

    g_hash_table_destroy(data);
    return FALSE;
}

gboolean
cam_control_get_sensor_data(MockControl *mock, GHashTable **data, GError **error)
{
    struct mock_state *state = mock->state;
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        char fourcc[] = FOURCC_STRING(state->sensor.format);
        cam_dbus_dict_add_string(dict, "name", state->sensor.name);
        cam_dbus_dict_add_uint(dict, "hMax", state->sensor.h_max_res);
        cam_dbus_dict_add_uint(dict, "vMax", state->sensor.v_max_res);
        cam_dbus_dict_add_uint(dict, "hMin", state->sensor.h_min_res);
        cam_dbus_dict_add_uint(dict, "vMin", state->sensor.v_min_res);
        cam_dbus_dict_add_uint(dict, "hIncrement", state->sensor.h_increment);
        cam_dbus_dict_add_uint(dict, "vIncrement", state->sensor.v_increment);
        cam_dbus_dict_add_uint(dict, "pixelRate", state->sensor.pixel_rate);
        cam_dbus_dict_add_string(dict, "pixelFormat", fourcc);
    }
    *data = dict;
    return (dict != NULL);
}

gboolean
cam_control_get_timing_limits(MockControl *mock, GHashTable *args, GHashTable **data, GError **error)
{
    struct mock_state *state = mock->state;
    struct image_constraints constraints;
    struct image_geometry geometry = {
        .hres = cam_dbus_dict_get_uint(args, "hRes", 0),
        .vres = cam_dbus_dict_get_uint(args, "vRes", 0),
        .hoffset = 0,
        .voffset = 0
    };
    GHashTable *dict;
    g_hash_table_destroy(args);
    
    if (image_sensor_get_constraints(&state->sensor, &geometry, &constraints) != 0) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid frame resolution and offset");
        return 0;
    }

    dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_uint(dict, "tMinPeriod", constraints.t_min_period);
        cam_dbus_dict_add_uint(dict, "tMaxPeriod", constraints.t_max_period);
        cam_dbus_dict_add_uint(dict, "tMinExposure", constraints.t_min_exposure);
        cam_dbus_dict_add_uint(dict, "tExposureDelay", constraints.t_exposure_delay);
        cam_dbus_dict_add_uint(dict, "tMaxShutterAngle", constraints.t_max_shutter);
        cam_dbus_dict_add_uint(dict, "fQuantization", constraints.f_quantization);
    }
    *data = dict;
    return (dict != NULL);
}
