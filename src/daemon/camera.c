
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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

#include "camera.h"
#include "utils.h"
#include "fpga.h"
#include "fpga-sensor.h"
#include "i2c.h"
#include "i2c-spd.h"

#define MAX_FRAME_LENGTH    0xf000
#define REC_START_ADDR      (MAX_FRAME_LENGTH * 4)

static inline int
cc_within(int x, int min, int max)
{
    if (x < min) return min;
    if (x < max) return max;
    return x;
}

static void
cam_set_color_matrix(struct fpga *fpga, const double *cc, const double *wb, double gain)
{
    double wb_gain[3] = {
        (4096.0 * wb[0] * gain),
        (4096.0 * wb[1] * gain),
        (4096.0 * wb[2] * gain),
    };

	fpga->reg[CCM_11] = cc_within(cc[0] * wb_gain[0], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->reg[CCM_12] = cc_within(cc[1] * wb_gain[0], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->reg[CCM_13] = cc_within(cc[2] * wb_gain[0], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);

	fpga->reg[CCM_21] = cc_within(cc[3] * wb_gain[1], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->reg[CCM_22] = cc_within(cc[4] * wb_gain[1], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->reg[CCM_23] = cc_within(cc[5] * wb_gain[1], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);

	fpga->reg[CCM_31] = cc_within(cc[6] * wb_gain[2], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->reg[CCM_32] = cc_within(cc[7] * wb_gain[2], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->reg[CCM_33] = cc_within(cc[8] * wb_gain[2], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
}

static const double ccm_default_color[] = {
    1.4508,     0.6010,     -0.8470,
    -0.5063,    1.3998,     0.0549,
    -0.0701,    -0.6060,	1.5849
};
static const double ccm_default_mono[] = {
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0,
    0.0, 0.0, 1.0
};
static const double wb_default_color[] = {
    0.8747, 1.0, 1.6607
};
static const double wb_default_mono[] = {
    1.0, 1.0, 1.0
};


/* TODO: Get the pixel clock and porch/sync period config from somewhere? */
void
cam_set_live_timing(struct image_sensor *sensor,
    unsigned int hRes, unsigned int vRes,
    unsigned int hOutRes, unsigned int vOutRes,
    unsigned int maxFps)
{
    struct fpga *fpga = sensor->fpga;
    const uint32_t pxClock = 100000000;
    const uint32_t hSync = 50;
    const uint32_t hPorch = 166;
    const uint32_t vSync = 3;
    const uint32_t vPorch = 38;
	uint32_t minHPeriod;
	uint32_t hPeriod;
	uint32_t vPeriod;
	uint32_t fps;

	hPeriod = hOutRes + hSync + hPorch + hSync;

	/*
     * Calculate minimum hPeriod to fit within the max vertical
     * resolution and make sure hPeriod is equal to or larger
     */
	minHPeriod = (pxClock / ((sensor->v_max_res + vPorch + vSync + vSync) * maxFps)) + 1; // the +1 is just to round up
	if (hPeriod < minHPeriod) hPeriod = minHPeriod;

	// calculate vPeriod and make sure it's large enough for the frame
    vPeriod = pxClock / (hPeriod * maxFps);
    if (vPeriod < (vOutRes + vSync + vPorch + vSync)) {
        vPeriod = (vOutRes + vSync + vPorch + vSync);
    }

	// calculate FPS for debug output
	fps = pxClock / (vPeriod * hPeriod);
	fprintf(stderr, "%s: %d*%d@%d (%d*%d max: %d)\n", __func__,
		   (hPeriod - hSync - hPorch - hSync), (vPeriod - vSync - vPorch - vSync), fps,
		   hOutRes, vOutRes, maxFps);

    fpga->display->h_res = hRes;
    fpga->display->h_out_res = hOutRes;
    fpga->display->v_res = vRes;
    fpga->display->v_out_res = vOutRes;

    fpga->display->h_period = hPeriod - 1;
    fpga->display->h_sync_len = hSync;
    fpga->display->h_back_porch = hPorch;

    fpga->display->v_period = vPeriod - 1;
    fpga->display->v_sync_len = vSync;
    fpga->display->v_back_porch = vPorch;
}

/* Camera Initialization */
int
cam_init(struct image_sensor *sensor)
{
    unsigned long long exposure;
    unsigned long long period;

    /* Configure the FIFO threshold and image sequencer */
    sensor->fpga->seq->live_addr[0] = MAX_FRAME_LENGTH;
    sensor->fpga->seq->live_addr[1] = MAX_FRAME_LENGTH*2;
    sensor->fpga->seq->live_addr[2] = MAX_FRAME_LENGTH*3;
    sensor->fpga->seq->region_start = REC_START_ADDR;

    /* Configure default default timing to the maximum resolution, framerate and exposure. */
    /* TODO: Configure Gain */
    period = image_sensor_round_period(sensor, sensor->h_max_res, sensor->v_max_res, 1000000000 / 60);
    exposure = image_sensor_round_exposure(sensor, sensor->h_max_res, sensor->v_max_res, 1000000000 / 60);
    image_sensor_set_resolution(sensor, sensor->h_max_res, sensor->v_max_res, 0, 0);
    image_sensor_set_period(sensor, sensor->h_max_res, sensor->v_max_res, period);
    image_sensor_set_exposure(sensor, sensor->h_max_res, sensor->v_max_res, exposure);

    cam_set_live_timing(sensor, sensor->h_max_res, sensor->v_max_res, sensor->h_max_res, sensor->v_max_res, 60);

    /* Load the default color correction and white balance matricies */
    /* TODO: Why are there two white balance matricies in the original code? */
    if (image_sensor_is_color(sensor)) {
        sensor->fpga->display->control |= DISPLAY_CTL_COLOR_MODE_MASK;
        cam_set_color_matrix(sensor->fpga, ccm_default_color, wb_default_color, 0);
    }
    else {
        sensor->fpga->display->control &= ~DISPLAY_CTL_COLOR_MODE_MASK;
        cam_set_color_matrix(sensor->fpga, ccm_default_mono, wb_default_mono, 0);
    }
    sensor->fpga->display->control &= ~DISPLAY_CTL_READOUT_INH_MASK;

    /* TODO: Can we safely ignore the trigger settings during init? */

    /* All is good, I think? */
    return 0;
} /* cam_init */
