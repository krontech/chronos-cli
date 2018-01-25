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

struct image_geometry {
    unsigned long hres;
    unsigned long vres;
    long hoffset;
    long voffset;
};

struct image_sensor_ops {
    unsigned long long (*round_exposure)(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
    unsigned long long (*round_period)(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
    int (*set_exposure)(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
    int (*set_period)(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
    int (*set_resolution)(struct image_sensor *sensor, const struct image_geometry *g);
    /* ADC Gain Configuration and Calibration */
    int (*set_gain)(struct image_sensor *sensor, int gain, FILE *cal);
    int (*cal_gain)(struct image_sensor *sensor, const struct image_geometry *g, const void *frame, FILE *cal);
    char *(*cal_suffix)(struct image_sensor *sensor, char *filename, size_t maxlen);
};

/*
 * See https://linuxtv.org/downloads/v4l-dvb-apis/uapi/v4l/videodev.html
 * for the pixel format definitions. In particular, look at the V4L2_PIX_FMT
 * macros for the definitive list of pixel formats and their corresponding
 * FOURCC codes.
 */
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
    unsigned long   adc_count;

    /* Black Pixel Regions. */
    unsigned long   blk_top;
    unsigned long   blk_bottom;
    unsigned long   blk_left;
    unsigned long   blk_right;
};

/* Init functions */
struct image_sensor *lux1310_init(struct fpga *fpga, const struct ioport *iop);

/* API Wrapper Calls */
int image_sensor_bpp(struct image_sensor *sensor);
int image_sensor_is_color(struct image_sensor *sensor);
int image_sensor_set_resolution(struct image_sensor *sensor, const struct image_geometry *g);
int image_sensor_set_period(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
int image_sensor_set_exposure(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
int image_sensor_set_gain(struct image_sensor *sensor, int gain, FILE *fp);
int image_sensor_cal_gain(struct image_sensor *sensor, const struct image_geometry *g, const void *frame, FILE *fp);
char *image_sensor_cal_suffix(struct image_sensor *sensor, char *buf, size_t maxlen);
unsigned long long image_sensor_round_exposure(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);
unsigned long long image_sensor_round_period(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec);

#endif /* _FPGA_SENSOR_H */
