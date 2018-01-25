
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

const double ccm_default_color[] = {
    1.4508,     0.6010,     -0.8470,
    -0.5063,    1.3998,     0.0549,
    -0.0701,    -0.6060,	1.5849
};
const double ccm_default_mono[] = {
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0,
    0.0, 0.0, 1.0
};
const double wb_default_color[] = {
    0.8747, 1.0, 1.6607
};
const double wb_default_mono[] = {
    1.0, 1.0, 1.0
};

static inline int
cc_within(int x)
{
    if (x < (-COLOR_MATRIX_MAXVAL)) return -COLOR_MATRIX_MAXVAL;
    if (x > (COLOR_MATRIX_MAXVAL-1)) return COLOR_MATRIX_MAXVAL-1;
    return x;
}

void
cal_update_color_matrix(CamObject *cam, double softgain)
{
    double wb_gain[3] = {
        (4096.0 * cam->wb_matrix[0] * softgain),
        (4096.0 * cam->wb_matrix[1] * softgain),
        (4096.0 * cam->wb_matrix[2] * softgain),
    };
    cam->softgain = softgain;
    
	cam->fpga->reg[CCM_11] = cc_within(cam->cc_matrix[0] * wb_gain[0]);
	cam->fpga->reg[CCM_12] = cc_within(cam->cc_matrix[1] * wb_gain[0]);
	cam->fpga->reg[CCM_13] = cc_within(cam->cc_matrix[2] * wb_gain[0]);

	cam->fpga->reg[CCM_21] = cc_within(cam->cc_matrix[3] * wb_gain[1]);
	cam->fpga->reg[CCM_22] = cc_within(cam->cc_matrix[4] * wb_gain[1]);
	cam->fpga->reg[CCM_23] = cc_within(cam->cc_matrix[5] * wb_gain[1]);

	cam->fpga->reg[CCM_31] = cc_within(cam->cc_matrix[6] * wb_gain[2]);
	cam->fpga->reg[CCM_32] = cc_within(cam->cc_matrix[7] * wb_gain[2]);
	cam->fpga->reg[CCM_33] = cc_within(cam->cc_matrix[8] * wb_gain[2]);
}

/* Helper function to pack pixels into memory despite inconvenient bit alignments. */
static inline void
cal_pixbuf_pack(uint32_t *buf, uint32_t pix, unsigned int bpp, unsigned int n)
{
    unsigned int index = (bpp * n) / 32;
    unsigned int shift = (bpp * n) % 32;
    buf[index] += pix << shift;
    if ((shift + bpp) > 32) {
        buf[index + 1] += pix >> (32 - shift);
    }
} /* cal_pixbuf_pack */

/*
 * Load the fixed-point noise calibration data and program it into the FPGA's RAM
 * space for FPN correction. Look for calibration data in the user calibration
 * directory first, then check the factory cal. Default to zeros if no cal file
 * was found.
 * 
 * The FPN file format is a raw frame captured from the image sensor with a fully
 * closed aperture, and stored at 16-bits per pixel. The FPN is then subtracted
 * from the raw image to correct any per-pixel offset.
 */
int
cal_load_fpn(CamObject *cam, struct image_geometry *g)
{
    unsigned int bpp = image_sensor_bpp(cam->sensor);
    uint32_t *output = (uint32_t *)cam->fpga->ram;
    uint16_t pixbuf[32];
    char suffix[NAME_MAX];
    unsigned int num;
    double softgain;
    uint16_t maxpix = 0;
    FILE *fp;

    /* Get the sensor-specific suffix for FPN data. */
    if (!image_sensor_cal_suffix(cam->sensor, suffix, sizeof(suffix))) {
        strcpy(suffix, "");
    }
    strcat(suffix, ".raw");

    /* Get the FPN file */
    do {
        char filename[PATH_MAX];
        
        /* Attempt to get the user FPN calibration first. */
        snprintf(filename, sizeof(filename), "userFPN/fpn_%lux%luoff%ldx%ld%s",
                        g->hres, g->vres, g->hoffset, g->voffset, suffix);
        fp = fopen(filename, "rb");
        if (fp) {
            break;
        }

        /* Fall back to the factory FPN calibration. */
        snprintf(filename, sizeof(filename), "cal/factoryFPN/fpn_%lux%luoff%ldx%ld%s",
                        g->hres, g->vres, g->hoffset, g->voffset, suffix);
        fp = fopen(filename, "rb");
        if (fp) {
            break;
        }

        /* If no FPN is present, flush the FPN memory to zero. */
        memset(output, 0, (g->hres * g->vres * bpp + 7) / 8);

        /* Otherwise, there is no calibration data to read. */
        return -ENOENT;
    } while(0);

    /*
     * Read FPN data in batches of 32 pixels, since this will always result in 
     * an integral number of 32-bit words to write out to the FPN memory for
     * any pixel depth.
     */
    while ((num = fread(pixbuf, sizeof(uint16_t), 32, fp)) != 0) {
        uint32_t writebuf[16];
        unsigned int i;
        memset(writebuf, 0, sizeof(writebuf));
        for (i = 0; i < num; i++) {
            if (pixbuf[i] > maxpix) maxpix = pixbuf[i];
            cal_pixbuf_pack(writebuf, pixbuf[i], bpp, i);
        }
        memcpy(output, writebuf, bpp * sizeof(uint32_t));
        output += bpp;
    }

    /* Recompute the gain calibration and update the color conversion matrix. */
    softgain = (double)(1 << bpp) / ((1<<bpp) - maxpix);
    fprintf(stderr, "DEBUG: Computed FPN softgain = %lf\n", softgain);
    cal_update_color_matrix(cam, softgain);

    fclose(fp);
    return 0;
} /* cal_load_fpn */

/*
 * Load the display column gain calibration file and store it into the FPGA DCG
 * registers. The DCG calibration applies a multiplication to the pixel columns
 * after FPN correction. This will attempt to correct for nonlinearity in the
 * column ADCs.
 * 
 * The file format is an array of IEEE 754 double-precision floating point
 * values in little-endian representation, one per ADC of the image sensor. 
 */
int
cal_load_dcg(CamObject *cam, int sensor_gain)
{
    double colgain[cam->sensor->adc_count];
    unsigned int i, num = 0;
    FILE *fp;

    /* Read the column gain calibration file. */
    if (sensor_gain > 12) {
        fp = fopen("cal/dcgH.bin", "rb");
    } else {
        fp = fopen("cal/dcgL.bin", "rb");
    }
    if (fp) {
        num = fread(colgain, sizeof(colgain[0]), cam->sensor->adc_count, fp);
        for (i = 0; i < cam->sensor->adc_count; i++) {
            fprintf(stderr, "DEBUG: DCG gain[%d] = %lf\n", i, colgain[i]);
            if ((colgain[i] < 0.5) || (colgain[i] > 2.0)) {
                num = 0; /* Out of range, don't use it. */
                break;
            }
        }
        fclose(fp);
    }

    /* Default column gain to 1.0 we didn't get the right number of columns. */
    if (num != cam->sensor->adc_count) {
        for (num = 0; num < cam->sensor->adc_count; num++) {
            colgain[num] = 1.0;
        }
    }

    /* Write the column gain adjustment settings. */
    for (i = 0; i < cam->sensor->h_max_res; i++) {
        cam->fpga->reg[0x800 + i] = (uint16_t)(4096 * colgain[i % cam->sensor->adc_count]);
    }

    return 0;
} /* cal_load_dcg */

/*
 * Load extra sensor calibration data, the format and purpose of which is
 * private to the image sensor driver.
 * 
 * Or at least that's the plan with this API, in the old QT app this file
 * gets used for column ADC offset calibration (hence the filename), but
 * this should go away and we should be able to do automatic offset cal
 * using the black regions.
 */
int
cal_load_gain(CamObject *cam, int gain)
{
    char suffix[NAME_MAX];
    char filename[PATH_MAX];
    int ret;
    FILE *fp;

    /* Get the sensor-specific suffix for calibration data. */
    if (!image_sensor_cal_suffix(cam->sensor, suffix, sizeof(suffix))) {
        strcpy(suffix, "");
    }
    snprintf(filename, sizeof(filename), "cal/%sOffsets%s.bin", cam->sensor->name, suffix);

    /* If we can open the file, let the image sensor read it for extra calibration. */
    fp = fopen(filename, "rb");
    ret = image_sensor_set_gain(cam->sensor, gain, fp);
    if (fp) {
        fclose(fp);
    }
    return ret;
}
