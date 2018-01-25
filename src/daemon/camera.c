
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

/* TODO: Get the pixel clock and porch/sync period config from somewhere? */
void
cam_set_live_timing(struct image_sensor *sensor,
    struct image_geometry *geometry,
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

    fpga->display->h_res = geometry->hres;
    fpga->display->h_out_res = hOutRes;
    fpga->display->v_res = geometry->vres;
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
cam_init(CamObject *cam)
{
    unsigned long long exposure;
    unsigned long long period;
    unsigned long frame_words;
    unsigned int maxfps = cam->sensor->pixel_rate / (cam->sensor->h_max_res * cam->sensor->v_max_res);
    struct image_geometry geometry = {
        .hres = cam->sensor->h_max_res,
        .vres = cam->sensor->v_max_res,
        .hoffset = 0,
        .voffset = 0,
    };

    /* Configure the FIFO threshold and image sequencer */
    cam->fpga->seq->live_addr[0] = MAX_FRAME_LENGTH;
    cam->fpga->seq->live_addr[1] = MAX_FRAME_LENGTH*2;
    cam->fpga->seq->live_addr[2] = MAX_FRAME_LENGTH*3;
    cam->fpga->seq->region_start = REC_START_ADDR;

    /* Configure default default timing to the maximum resolution, framerate and exposure. */
    /* TODO: Configure Gain */
    period = image_sensor_round_period(cam->sensor, &geometry, 1000000000 / maxfps);
    exposure = image_sensor_round_exposure(cam->sensor, &geometry, 1000000000 / (maxfps * 2));
    image_sensor_set_resolution(cam->sensor, &geometry);
    image_sensor_set_period(cam->sensor, &geometry, period);
    image_sensor_set_exposure(cam->sensor, &geometry, exposure);
    frame_words = ((cam->sensor->h_max_res * cam->sensor->v_max_res * image_sensor_bpp(cam->sensor)) / 8 + (32 - 1)) / 32;
    cam->fpga->seq->frame_size = (frame_words + 0x3f) & ~0x3f;

    /* Load calibration data for the default resolution. */
    cal_load_fpn(cam, &geometry);
    cal_load_dcg(cam, 0);
    cal_load_gain(cam, 0);

    /* Load the default color correction and white balance matricies */
    /* TODO: Why are there two white balance matricies in the original code? */
    if (image_sensor_is_color(cam->sensor)) {
        cam->fpga->display->control |= DISPLAY_CTL_COLOR_MODE;
        memcpy(cam->cc_matrix, ccm_default_color, sizeof(cam->cc_matrix));
        memcpy(cam->wb_matrix, wb_default_color, sizeof(cam->wb_matrix));
        cal_update_color_matrix(cam, 1.0);
    }
    else {
        cam->fpga->display->control &= ~DISPLAY_CTL_COLOR_MODE;
        memcpy(cam->cc_matrix, ccm_default_mono, sizeof(cam->cc_matrix));
        memcpy(cam->wb_matrix, wb_default_mono, sizeof(cam->wb_matrix));
        cal_update_color_matrix(cam, 1.0);
    }
    cam->fpga->display->control &= ~DISPLAY_CTL_READOUT_INHIBIT;

    cam_set_live_timing(cam->sensor, &geometry, cam->sensor->h_max_res, cam->sensor->v_max_res, 60);

    /* TODO: Can we safely ignore the trigger settings during init? */
    /* All is good, I think? */
    return 0;
} /* cam_init */
