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
#include <glib-object.h>

#include "mock.h"
#include "fpga-sensor.h"

/* Magical constants pretending to be hardware. */
#define MOCK_MAX_HRES           1920
#define MOCK_MAX_VRES           1080
#define MOCK_MIN_HRES           256
#define MOCK_MIN_VRES           64
#define MOCK_MAX_FRAMERATE      1000
#define MOCK_MAX_PIXELRATE      (MOCK_MAX_HRES * MOCK_MAX_VRES * MOCK_MAX_FRAMERATE)
#define MOCK_QUANTIZE_TIMING    250
#define MOCK_MAX_EXPOSURE       1000000000
#define MOCK_MIN_EXPOSURE       1000
#define MOCK_MAX_SHUTTER_ANGLE  330

#define MOCK_VRES_INCREMENT     2
#define MOCK_HRES_INCREMENT     32

static int
mock_constraints(struct image_sensor *sensor, const struct image_geometry *g, struct image_constraints *c)
{
    c->t_max_period = UINT32_MAX;
    c->t_min_period = (g->hres * g->vres * 1000000000ULL) / MOCK_MAX_PIXELRATE;
    c->f_quantization = (1000000000UL / MOCK_QUANTIZE_TIMING);

    c->t_min_exposure = MOCK_MIN_EXPOSURE;
    c->t_max_shutter = MOCK_MAX_SHUTTER_ANGLE;
    c->t_exposure_delay = MOCK_MIN_EXPOSURE;

    return 0;
}

/* Mock image sensor operations. */
static const struct image_sensor_ops mock_ops = {
    .get_constraints = mock_constraints,
#if 0
    int (*set_exposure)(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
    int (*set_period)(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
    int (*set_resolution)(struct image_sensor *sensor, const struct image_geometry *g);
    /* ADC Gain Configuration and Calibration */
    int (*set_gain)(struct image_sensor *sensor, int gain, FILE *cal);
    int (*cal_gain)(struct image_sensor *sensor, const struct image_geometry *g, const void *frame, FILE *cal);
    char *(*cal_suffix)(struct image_sensor *sensor, char *filename, size_t maxlen);
#endif
};

void
mock_sensor_init(struct image_sensor *sensor)
{
    sensor->ops = &mock_ops;

    /* Image Sensor descriptions. */
    sensor->name = "acme9001";
    sensor->mfr = "ACME Industries";
    sensor->format = FOURCC_CODE('B', 'Y', 'R', '2');

    sensor->h_max_res = MOCK_MAX_HRES;
    sensor->v_max_res = MOCK_MAX_VRES;
    sensor->h_min_res = MOCK_MIN_HRES;
    sensor->v_min_res = MOCK_MIN_VRES;
    sensor->h_increment = MOCK_HRES_INCREMENT;
    sensor->v_increment = MOCK_VRES_INCREMENT;
    /* TODO: Frame/Exposure timing should go into a separate call. */
    sensor->pixel_rate = MOCK_MAX_PIXELRATE;
    sensor->adc_count = MOCK_HRES_INCREMENT;

    /* No black pixel regions. */
    sensor->blk_top = 0;
    sensor->blk_bottom = 0;
    sensor->blk_left = 0;
    sensor->blk_right = 0;
}
