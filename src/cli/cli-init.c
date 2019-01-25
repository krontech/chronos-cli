/****************************************************************************
 *  Copyright (C) 2017 Kron Technologies Inc <http://www.krontech.ca>.      *
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

#include "cli.h"
#include "utils.h"
#include "fpga.h"
#include "lux1310.h"
#include "i2c.h"
#include "i2c-spd.h"

static inline int
cc_within(int x, int min, int max)
{
    if (x < min) return min;
    if (x < max) return max;
    return x;
}

static void
fpga_set_color_matrix(struct fpga *fpga, const double *cc, const double *wb, double gain)
{
    double wb_gain[3] = {
        (4096.0 * wb[0] * gain),
        (4096.0 * wb[1] * gain),
        (4096.0 * wb[2] * gain),
    };

	fpga->display->ccm_red[0] = cc_within(cc[0] * wb_gain[0], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->display->ccm_red[1] = cc_within(cc[1] * wb_gain[0], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->display->ccm_red[2] = cc_within(cc[2] * wb_gain[0], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);

	fpga->display->ccm_green[0] = cc_within(cc[3] * wb_gain[1], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->display->ccm_green[1] = cc_within(cc[4] * wb_gain[1], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->display->ccm_green[2] = cc_within(cc[5] * wb_gain[1], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);

	fpga->display->ccm_blue[0] = cc_within(cc[6] * wb_gain[2], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->display->ccm_blue[1] = cc_within(cc[7] * wb_gain[2], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
	fpga->display->ccm_blue[2] = cc_within(cc[8] * wb_gain[2], -COLOR_MATRIX_MAXVAL, COLOR_MATRIX_MAXVAL-1);
} /* fpga_set_color_matrix */

static int
do_init(struct fpga *fpga, char *const argv[], int argc)
{
    unsigned int ram_size0 = 0;
    unsigned int ram_size1 = 0;
    struct ddr3_spd spd;
    int is_color = gpio_read(fpga->gpio.color_sel);
    int fd;

    double cc_matrix[9];
    double wb_matrix[3];

    /* Bring up the default video timing */
    fpga->display->h_period = 1296+50+50+166-1;
    fpga->display->v_period = 1024+1+3+38-1;
    fpga->display->h_sync_len = 50;
    fpga->display->v_sync_len = 3;
    fpga->display->h_back_porch = 166;
    fpga->display->v_back_porch = 38;
    /* TODO: FIFO threshold */
    /* TODO: Sequencer */

    /* Read the I2C SPD data, and program the FPGA's MMU to remap it
     * into a contiguous block. */
	if ((fd = open(EEPROM_I2C_BUS, O_RDWR)) < 0) {
		fprintf(stderr, "Failed to open i2c bus (%s)\n", strerror(errno));
	    return -1;
    }
    if (i2c_eeprom_read(fd, SPD_I2C_ADDR(0), 0, &spd, sizeof(spd)) >= 0) {
        ram_size0 = spd_size_bytes(&spd) >> 30;
    }
    if (i2c_eeprom_read(fd, SPD_I2C_ADDR(1), 0, &spd, sizeof(spd)) >= 0) {
        ram_size1 = spd_size_bytes(&spd) >> 30;
    }

    if (ram_size1 > ram_size0) {
        fpga->config->mmu_config = MMU_INVERT_CS;
        fprintf(stderr, "MMU: Invert CS remap\n");
    }
    else if ((ram_size0 < 16) && (ram_size1 < 16)) {
        fpga->config->mmu_config = MMU_SWITCH_STUFFED;
        fprintf(stderr, "MMU: switch stuffed remap\n");
    }
    else {
        fpga->config->mmu_config = 0;
        fprintf(stderr, "MMU: no remap applied\n");
    }

    /* Enable video readout */

    /* TODO: Sensor init? */

    /* TODO: Imager Settings configuration. */

    /* Load the default color correction and white balance matricies */
    if (is_color) {
        fpga->display->control |= DISPLAY_CTL_COLOR_MODE_MASK;

        /* LUX1310 color defaults */
		cc_matrix[0] = 1.4508;	cc_matrix[1] = 0.6010;	cc_matrix[2] = -0.8470;
		cc_matrix[3] = -0.5063;	cc_matrix[4] = 1.3998;	cc_matrix[5] = 0.0549;
		cc_matrix[6] = -0.0701;	cc_matrix[7] = -0.6060;	cc_matrix[8] = 1.5849;

        wb_matrix[0] = 0.8748;	wb_matrix[1] = 1.0;	    wb_matrix[2] = 1.6607;
    }
    else {
        fpga->display->control &= ~DISPLAY_CTL_COLOR_MODE_MASK;

        /* LUX1310 mono defaults. */
		cc_matrix[0] = 1.0;	cc_matrix[1] = 0.0;	cc_matrix[2] = 0.0;
		cc_matrix[3] = 0.0;	cc_matrix[4] = 1.0;	cc_matrix[5] = 0.0;
        cc_matrix[6] = 0.0;	cc_matrix[7] = 0.0;	cc_matrix[8] = 1.0;
        
		wb_matrix[0] = 1.0;	wb_matrix[1] = 1.0;	wb_matrix[2] = 1.0;
    }
    /* TODO: Why are there two white balance matricies in the original code? */
    fpga_set_color_matrix(fpga, cc_matrix, wb_matrix, 0);

    fpga->display->control &= ~DISPLAY_CTL_READOUT_INH_MASK;

    /* TODO: Can we safely ignore the trigger settings during init? */

    /* All is good, I think? */
    return 0;
} /* do_init */

/* The lux1310 subcommand */
const struct cli_subcmd cli_cmd_init = {
    .name = "init",
    .desc = "Perform camera initialization.",
    .function = do_init,
};
