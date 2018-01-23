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
#ifndef _FPGA_SENSOR_H
#define _FPGA_SENSOR_H

#include <stdint.h>
#include <stdio.h>

#include "ioport.h"
#include "fpga.h"

struct image_sensor;

struct image_sensor_ops {
    unsigned long long (*round_exposure)(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
    unsigned long long (*round_period)(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
    int (*set_exposure)(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
    int (*set_period)(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
    int (*set_resolution)(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long hoff, unsigned long voff);
    int (*set_gain)(struct image_sensor *sensor, int gain);
    /* Calibration API - needs thinking??? not sure how to handle column AGC */
    char *(*cal_filename)(struct image_sensor *sensor, char *buf, size_t maxlen);
    int (*cal_write)(struct image_sensor *sensor, FILE *fp);
    int (*cal_read)(struct image_sensor *sensor, FILE *fp);
};

#define FOURCC_CODE(_a_, _b_, _c_, _d_) \
    (((uint32_t)(_a_) << 0) | ((uint32_t)(_b_) << 8) | ((uint32_t)(_c_) << 16) | ((uint32_t)(_d_) << 24))

struct image_sensor {
    struct fpga *fpga;
    const struct image_sensor_ops *ops;

    /* Image Sensor descriptions. */
    const char      *name;
    const char      *mfr;
    uint32_t        format;
    
    /* Image Sensor Limits */
    unsigned long   h_max_res;
    unsigned long   v_max_res;
    unsigned long   h_min_res;
    unsigned long   v_min_res;
    unsigned long   h_increment;
    unsigned long   v_increment;
    unsigned long long  exp_min_nsec;
    unsigned long long  exp_max_nsec;
    unsigned long long  pixel_rate;
};

/* Init functions */
struct image_sensor *lux1310_init(struct fpga *fpga, const struct ioport *iop);

/* API Wrapper Calls */
int image_sensor_is_color(struct image_sensor *sensor);
int image_sensor_set_resolution(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long hoff, unsigned long voff);
int image_sensor_set_period(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
int image_sensor_set_exposure(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
unsigned long long image_sensor_round_exposure(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);
unsigned long long image_sensor_round_period(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec);

#endif /* _FPGA_SENSOR_H */
