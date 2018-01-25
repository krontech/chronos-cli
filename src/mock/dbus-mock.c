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
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */
static gboolean cam_dbus_get_video_settings(MockObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_set_video_settings(MockObject *cam, GHashTable *data, GError **error);
static gboolean cam_dbus_get_camera_data(MockObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_get_sensor_data(MockObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_get_timing_limits(MockObject *cam, gint hres, gint vres, GHashTable **data, GError *error);

#include "api/cam-dbus-server.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */
static void
mock_object_init(MockObject *mock)
{
    g_assert(mock != NULL);
    memset((char *)mock + sizeof(mock->parent), 0, sizeof(MockObject) - sizeof(mock->parent));

    mock->hres = MOCK_MAX_HRES;
    mock->vres = MOCK_MAX_VRES;
    mock->hoff = 0;
    mock->voff = 0;
    mock->period_nsec = 1000000000 / MOCK_MAX_FRAMERATE;
    mock->period_nsec = mock->period_nsec * MOCK_MAX_SHUTTER_ANGLE / 360;
    mock->gain_db = 0;
}

static void
mock_object_class_init(MockObjectClass *mclass)
{
    g_assert(mclass != NULL);
    dbus_g_object_type_install_info(MOCK_OBJECT_TYPE, &dbus_glib_cam_dbus_object_info);
}

#define MOCK_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_CAST((_obj_), MOCK_OBJECT_TYPE, MockObject))
#define CAM_OBJECT_CLASS(_mclass_) \
    (G_TYPE_CHECK_CLASS_CAST((_mclass_), MOCK_OBJECT_TYPE, MockObjectClass))
#define CAM_IS_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_TYPE(_obj_), MOCK_OBJECT_TYPE))
#define CAM_IS_CLASS(_mclass_) \
    (G_TYPE_CHECK_CLASS_TYPE(_mclass_), MOCK_OBJECT_TYPE))
#define CAM_GET_CLASS(_obj_) \
    (G_TYPE_INSTANCE_GET_CLASS(_obj_), MOCK_OBJECT_TYPE, MockObjectClass))

G_DEFINE_TYPE(MockObject, mock_object, G_TYPE_OBJECT)

/*-------------------------------------
 * DBUS API Calls
 *-------------------------------------
 */
static gboolean
cam_dbus_get_camera_data(MockObject *mock, GHashTable **data, GError **error)
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

static gboolean
cam_dbus_get_video_settings(MockObject *mock, GHashTable **data, GError **error)
{
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
}

#define CAM_ERROR_INVALID_PARAMETERS    g_quark_from_static_string("cam-invalid-parameters-quark")

static gboolean
cam_dbus_set_video_settings(MockObject *mock, GHashTable *data, GError **error)
{
    do {
        unsigned int hres = cam_dbus_dict_get_uint(data, "hRes", 0);
        unsigned int vres = cam_dbus_dict_get_uint(data, "vRes", 0);
        unsigned int hoff = cam_dbus_dict_get_uint(data, "hOffset", 0);
        unsigned int voff = cam_dbus_dict_get_uint(data, "vOffset", 0);
        unsigned int exp_nsec = cam_dbus_dict_get_uint(data, "exposureNsec", 0);
        unsigned int period_nsec = cam_dbus_dict_get_uint(data, "periodNsec", 0);
        int gain = cam_dbus_dict_get_int(data, "gain", 0);

        /* Sanity check the hres and vres */
        if ( ((hres < MOCK_MIN_HRES) || (hres > MOCK_MAX_HRES)) ||
             ((vres < MOCK_MIN_VRES) || (vres > MOCK_MAX_VRES)) ||
             ((hres % MOCK_HRES_INCREMENT) || (vres % MOCK_VRES_INCREMENT))) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Invalid Frame Resolution");
            break;
        }
        if ((hoff % MOCK_HRES_INCREMENT) || (voff % MOCK_VRES_INCREMENT)) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Invalid Frame Offsets");
            break;
        }
        
        /* Sanity check the frame timing. */
        if (period_nsec == 0) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Unspecified Frame Period");
            break;
        }
        period_nsec += MOCK_QUANTIZE_TIMING-1;
        period_nsec -= (period_nsec % MOCK_QUANTIZE_TIMING);
        if ((hres * vres * 1000000000 / period_nsec) > MOCK_MAX_PIXELRATE) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Frame Period Too Short for Frame Size");
            break;
        }

        /* Sanity check the exposure time. */
        if (exp_nsec == 0) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Unspecified Exposure Time");
            break;
        }
        if (exp_nsec > MOCK_MAX_EXPOSURE) exp_nsec = MOCK_MAX_EXPOSURE;
        if (exp_nsec < MOCK_MIN_EXPOSURE) exp_nsec = MOCK_MIN_EXPOSURE;
        exp_nsec += MOCK_QUANTIZE_TIMING-1;
        exp_nsec -= (exp_nsec % MOCK_QUANTIZE_TIMING);
        if (exp_nsec > (period_nsec * MOCK_MAX_SHUTTER_ANGLE) / 360) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Exposure Time Exceeds Maximum Shutter Angle");
            break;
        }

        /* The gain defaults to zero if omitted, but otherwise must be a multiple of 6dB and less than MAX_GAIN */
        if ((gain % 6) || (gain > MOCK_MAX_GAIN) || (gain < 0)) {
            *error = g_error_new(CAM_ERROR_INVALID_PARAMETERS, 0, "Invalid Gain");
            break;
        }
        /* Store it */
        mock->hres = hres;
        mock->vres = vres;
        mock->hoff = hoff;
        mock->voff = voff;
        mock->exposure_nsec;
        mock->period_nsec;
        mock->gain_db = gain;

        g_hash_table_destroy(data);
        return TRUE;
    } while (0);

    g_hash_table_destroy(data);
    return FALSE;
}

static gboolean
cam_dbus_get_sensor_data(MockObject *mock, GHashTable **data, GError **error)
{
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_string(dict, "name", "lux9001");
        cam_dbus_dict_add_uint(dict, "hMax", MOCK_MAX_HRES);
        cam_dbus_dict_add_uint(dict, "vMax", MOCK_MAX_VRES);
        cam_dbus_dict_add_uint(dict, "hMin", MOCK_MIN_HRES);
        cam_dbus_dict_add_uint(dict, "vMin", MOCK_MIN_VRES);
        cam_dbus_dict_add_uint(dict, "hIncrement", MOCK_HRES_INCREMENT);
        cam_dbus_dict_add_uint(dict, "vIncrement", MOCK_VRES_INCREMENT);
        cam_dbus_dict_add_uint(dict, "minExposureNsec", 1000);
        cam_dbus_dict_add_uint(dict, "maxExposureNsec", UINT32_MAX);
        cam_dbus_dict_add_uint(dict, "pixelRate", MOCK_MAX_PIXELRATE);
        cam_dbus_dict_add_string(dict, "pixelFormat", "BYR2");
    }
    *data = dict;
    return (dict != NULL);
}

static gboolean
cam_dbus_get_timing_limits(MockObject *mock, gint hres, gint vres, GHashTable **data, GError *error)
{
    /* TODO: This needs a bunch of thinking on how to convey the timing limits.
     * Real image sensors have a target frame period, in which the blanking
     * intervals and some exposure timing overhead must fit along with the
     * time to clock the pixels out of the sensor.
     * 
     * Can we therefore describe the timing bound as:
     *      tPixelData = (nRows * nCols) / pixelrate
     *      tFrame > tPixelData + (nRow * tRowOverhead) + tGlobalOverhead
     *      tExposure < tFrame - tExposureOverhead
     * 
     * In the particular case of the LUX1310, we would get:
     *      pixelrate = Tclk * 16 = 1.44GP/s
     *      tRowOverhead = tHblank
     *      tGlobalOverhead = tTx + tFovf + tFovb
     *      tExposureOverhead = tAbn
     * 
     * What then defines the minimum tFrame, and ultimately the max framerate?
     */
    GHashTable *dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_uint(dict, "minPeriodNsec", (hres * vres * 1000000000ULL) / MOCK_MAX_PIXELRATE);
        cam_dbus_dict_add_uint(dict, "maxPeriodNsec", UINT32_MAX);
        cam_dbus_dict_add_uint(dict, "minExposureNsec", MOCK_MIN_EXPOSURE);
        cam_dbus_dict_add_uint(dict, "maxExposureNsec", MOCK_MAX_EXPOSURE);
    }
    *data = dict;
    return (dict != NULL);
}
