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
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid Frame Resolution");
            break;
        }
        if ((hoff % MOCK_HRES_INCREMENT) || (voff % MOCK_VRES_INCREMENT)) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid Frame Offsets");
            break;
        }
        
        /* Sanity check the frame timing. */
        if (period_nsec == 0) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Unspecified Frame Period");
            break;
        }
        period_nsec += MOCK_QUANTIZE_TIMING-1;
        period_nsec -= (period_nsec % MOCK_QUANTIZE_TIMING);
        if ((hres * vres * 1000000000 / period_nsec) > MOCK_MAX_PIXELRATE) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Frame Period Too Short for Frame Size");
            break;
        }

        /* Sanity check the exposure time. */
        if (exp_nsec == 0) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Unspecified Exposure Time");
            break;
        }
        if (exp_nsec > MOCK_MAX_EXPOSURE) exp_nsec = MOCK_MAX_EXPOSURE;
        if (exp_nsec < MOCK_MIN_EXPOSURE) exp_nsec = MOCK_MIN_EXPOSURE;
        exp_nsec += MOCK_QUANTIZE_TIMING-1;
        exp_nsec -= (exp_nsec % MOCK_QUANTIZE_TIMING);
        if (exp_nsec > (period_nsec * MOCK_MAX_SHUTTER_ANGLE) / 360) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Exposure Time Exceeds Maximum Shutter Angle");
            break;
        }

        /* The gain defaults to zero if omitted, but otherwise must be a multiple of 6dB and less than MAX_GAIN */
        if ((gain % 6) || (gain > MOCK_MAX_GAIN) || (gain < 0)) {
            *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid Gain");
            break;
        }
        /* Store it */
        state->hres = hres;
        state->vres = vres;
        state->hoff = hoff;
        state->voff = voff;
        state->exposure_nsec;
        state->period_nsec;
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

gboolean
cam_control_get_timing_limits(MockControl *mock, GHashTable *args, GHashTable **data, GError **error)
{
    unsigned int hres = cam_dbus_dict_get_uint(args, "hRes", 0);
    unsigned int vres = cam_dbus_dict_get_uint(args, "vRes", 0);
    GHashTable *dict;
    g_hash_table_destroy(args);
    
    if ( ((hres < MOCK_MIN_HRES) || (hres > MOCK_MAX_HRES)) ||
         ((vres < MOCK_MIN_VRES) || (vres > MOCK_MAX_VRES)) ||
         ((hres % MOCK_HRES_INCREMENT) || (vres % MOCK_VRES_INCREMENT))) {
        *error = g_error_new(CAM_ERROR_PARAMETERS, 0, "Invalid Frame Resolution");
        return 0;
    }

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
     * 
     * So then, as an API model, we'll do the following.
     *      caller provides X/Y frame size, and the daemon returns:
     *          - tMinFramePeriod = minimum frame period for this resolution.
     *          - tMaxFramePeriod = maximum frame period for this resolution.
     *          - tMaxShutterAngle = maximum shutter angle in degress.
     *          - tMinExposure = minimum exposure time.
     *          - tExposureOverhead = timing overhead required for exposure (see equation below).
     *          - fQuantization = clock rate used for timing quantization.
     * 
     * Thus, frame period must be constrainted by:
     *      tMinFramePeriod <= tFramePeriod <= tMaxFramePeriod
     * 
     * And, exposure time must be constrainte
int
main(void)
{
    g_type_init();
    mock = g_object_new(MOCK_OBJECT_TYPE, NULL);
    mock_main(mock, CAM_DBUS_CONTROL_SERVICE, CAM_DBUS_CONTROL_PATH);
}
d by:
     *      tMinExposure <= tExposure <= (tFramePeriod * tMaxShutterAngle) / 360 - tExposureOverhead
     * 
     * The quantization frequency is optional, and provides a hint to the quantization behavior of
     * the underlying exposure and frame timing. Exposure and frame timing will be rounded to an
     * integer multiple of (1/fQuantization) seconds.
     */
    dict = cam_dbus_dict_new();
    if (dict) {
        cam_dbus_dict_add_uint(dict, "tMinPeriod", (hres * vres * 1000000000ULL) / MOCK_MAX_PIXELRATE);
        cam_dbus_dict_add_uint(dict, "tMaxPeriod", UINT32_MAX);
        cam_dbus_dict_add_uint(dict, "tMinExposure", MOCK_MIN_EXPOSURE);
        cam_dbus_dict_add_uint(dict, "tExposureOverhead", MOCK_MIN_EXPOSURE);
        cam_dbus_dict_add_uint(dict, "tMaxShutterAngle", MOCK_MAX_SHUTTER_ANGLE);
        cam_dbus_dict_add_uint(dict, "fQuantization", 1000000000 / MOCK_QUANTIZE_TIMING);
    }
    *data = dict;
    return (dict != NULL);
}
