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
image_sensor_is_color(struct image_sensor *sensor)
{
    switch (sensor->format) {
        case FOURCC_CODE('G', 'R', 'E', 'Y'):
        case FOURCC_CODE('Y', '0', '4', ' '):
        case FOURCC_CODE('Y', '0', '6', ' '):
        case FOURCC_CODE('Y', '1', '0', ' '):
        case FOURCC_CODE('Y', '1', '2', ' '):
        case FOURCC_CODE('Y', '1', '6', ' '):
            return 0;
        default:
            return 1;
    }
}

unsigned long long
image_sensor_round_exposure(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec)
{
    if (sensor->ops->round_exposure) {
        return sensor->ops->round_exposure(sensor, hres, vres, nsec);
    } else {
        return nsec;
    }
}

unsigned long long
image_sensor_round_period(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec)
{
    if (sensor->ops->round_exposure) {
        return sensor->ops->round_exposure(sensor, hres, vres, nsec);
    } else {
        return nsec;
    }
}

int
image_sensor_set_exposure(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec)
{
    if (sensor->ops->set_exposure) {
        return sensor->ops->set_exposure(sensor, hres, vres, nsec);
    } else {
        return 0;
    }
}

int
image_sensor_set_period(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec)
{
    if (sensor->ops->set_period) {
        return sensor->ops->set_period(sensor, hres, vres, nsec);
    } else {
        return 0;
    }
}

int
image_sensor_set_resolution(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long hoff, unsigned long voff)
{
    if (sensor->ops->set_resolution) {
        return sensor->ops->set_resolution(sensor, hres, vres, hoff, voff);
    }
    if ((hres > sensor->h_max_res) || (hres < sensor->h_min_res) || (hres % sensor->h_increment)) {
        return -ERANGE;
    }
    if ((vres > sensor->v_max_res) || (vres < sensor->v_min_res) || (vres % sensor->v_increment)) {
        return -ERANGE;
    }
    return 0;
}
