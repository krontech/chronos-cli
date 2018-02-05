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
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include "fpga-sensor.h"

int
image_sensor_bpp(struct image_sensor *sensor)
{
    switch (sensor->format) {
        case FOURCC_CODE('Y', '0', '4', ' '):
            return 4;
        case FOURCC_CODE('Y', '0', '6', ' '):
            return 6;
        
        /* 10-bit bayer formats. */
        case FOURCC_CODE('B', 'G', '1', '0'):
        case FOURCC_CODE('G', 'B', '1', '0'):
        case FOURCC_CODE('B', 'A', '1', '0'):
        case FOURCC_CODE('R', 'G', '1', '0'):
        /* 10-bit greyscale formats. */
        case FOURCC_CODE('Y', '1', '0', ' '):
        case FOURCC_CODE('Y', '1', '0', 'B'):
            return 10;

        /* 12-bit bayer formats. */
        case FOURCC_CODE('B', 'G', '1', '2'):
        case FOURCC_CODE('G', 'B', '1', '2'):
        case FOURCC_CODE('B', 'A', '1', '2'):
        case FOURCC_CODE('R', 'G', '1', '2'):
        /* 12-bit greyscale formats. */
        case FOURCC_CODE('Y', '1', '2', ' '):
            return 12;

        /* 16-bit bayer formats. */
        case FOURCC_CODE('B', 'Y', 'R', '2'):
        case FOURCC_CODE('G', 'B', '1', '6'):
        case FOURCC_CODE('G', 'R', '1', '6'):
        case FOURCC_CODE('R', 'G', '1', '6'):
        /* 16-bit greyscale formats. */
        case FOURCC_CODE('Y', '1', '6', ' '):
            return 16;

        /* 8-bit bayer formats */
        case FOURCC_CODE('B', 'A', '8', '1'):
        case FOURCC_CODE('B', 'Y', 'R', '1'):
        case FOURCC_CODE('G', 'B', 'R', 'G'):
        case FOURCC_CODE('G', 'R', 'B', 'G'):
        case FOURCC_CODE('R', 'G', 'G', 'B'):
        /* 8-bit greyscale formats. */
        case FOURCC_CODE('G', 'R', 'E', 'Y'):
        default:
            return 8;
    }
}

int
image_sensor_is_color(struct image_sensor *sensor)
{
    switch (sensor->format) {
        case FOURCC_CODE('G', 'R', 'E', 'Y'):
        case FOURCC_CODE('Y', '0', '4', ' '):
        case FOURCC_CODE('Y', '0', '6', ' '):
        case FOURCC_CODE('Y', '1', '0', ' '):
        case FOURCC_CODE('Y', '1', '0', 'B'):
        case FOURCC_CODE('Y', '1', '2', ' '):
        case FOURCC_CODE('Y', '1', '6', ' '):
            return 0;
        default:
            return 1;
    }
}

unsigned long long
image_sensor_max_exposure(const struct image_constraints *c, unsigned long long t_period)
{
    return ((t_period * c->t_max_shutter) / 360) - c->t_exposure_delay;
}

int
image_sensor_set_exposure(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec)
{
    if (sensor->ops->set_exposure) {
        return sensor->ops->set_exposure(sensor, g, nsec);
    } else {
        return 0;
    }
}

int
image_sensor_set_period(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec)
{
    if (sensor->ops->set_period) {
        return sensor->ops->set_period(sensor, g, nsec);
    } else {
        return 0;
    }
}

static int
image_sensor_valid_resolution(struct image_sensor *sensor, const struct image_geometry *g)
{
    if ((g->hres > sensor->h_max_res) || (g->hres < sensor->h_min_res)) {
        return -ERANGE;
    }
    if ((g->hres % sensor->h_increment) || (g->hoffset % sensor->h_increment)) {
        return -ERANGE;
    }
    if ((g->vres > sensor->v_max_res) || (g->vres < sensor->v_min_res)) {
        return -ERANGE;
    }
    if ((g->vres % sensor->v_increment) || (g->voffset % sensor->v_increment)) {
        return -ERANGE;
    }
    return 0;
}

int
image_sensor_set_resolution(struct image_sensor *sensor, const struct image_geometry *g)
{
    int err = image_sensor_valid_resolution(sensor, g);
    if (err != 0) {
        return err;
    }
    if (sensor->ops->set_resolution) {
        return sensor->ops->set_resolution(sensor, g);
    }
    return 0;
}

int
image_sensor_get_constraints(struct image_sensor *sensor, const struct image_geometry *g, struct image_constraints *c)
{
    int err = image_sensor_valid_resolution(sensor, g);
    if (err != 0) {
        return err;
    }
    if (sensor->ops->get_constraints) {
        return sensor->ops->get_constraints(sensor, g, c);
    }
    return -EINVAL;
}

int
image_sensor_set_gain(struct image_sensor *sensor, int gain, FILE *fp)
{
    if (sensor->ops->set_gain) {
        return sensor->ops->set_gain(sensor, gain, fp);
    } else {
        return 0;
    }
}

int
image_sensor_cal_gain(struct image_sensor *sensor, const struct image_geometry *g, const void *frame, FILE *fp)
{
    if (sensor->ops->cal_gain) {
        return sensor->ops->cal_gain(sensor, g, frame, fp);
    } else {
        return 0;
    }
}

char *
image_sensor_cal_suffix(struct image_sensor *sensor, char *buf, size_t maxlen)
{
    if (sensor->ops->cal_suffix) {
        return sensor->ops->cal_suffix(sensor, buf, maxlen);
    } else {
        return strcpy(buf, "");
    }
}
