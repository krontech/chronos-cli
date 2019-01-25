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

/* DDR initialization */
unsigned int
mem_init(struct fpga *fpga)
{
    unsigned int ram_size0 = 0;
    unsigned int ram_size1 = 0;
    struct ddr3_spd spd;
    int fd;

    /* Read the I2C SPD data, and program the FPGA's MMU to remap it
     * into a contiguous block. */
	if ((fd = open(EEPROM_I2C_BUS, O_RDWR)) < 0) {
		fprintf(stderr, "Failed to open i2c bus (%s)\n", strerror(errno));
	    return 0;
    }
    if (i2c_eeprom_read(fd, SPD_I2C_ADDR(0), 0, &spd, sizeof(spd)) >= 0) {
        ram_size0 = spd_size_bytes(&spd) >> 30;
    }
    if (i2c_eeprom_read(fd, SPD_I2C_ADDR(1), 0, &spd, sizeof(spd)) >= 0) {
        ram_size1 = spd_size_bytes(&spd) >> 30;
    }
    close(fd);

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
    return (ram_size0 + ram_size1);
} /* mem_init */
