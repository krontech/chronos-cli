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
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include "cli.h"
#include "fpga.h"
#include "i2c.h"
#include "i2c-spd.h"

#define LUX1310_COLOR_DETECT        "/sys/class/gpio/gpio34/value"
#define EEPROM_I2C_BUS              "/dev/i2c-1"
#define EEPROM_I2C_ADDR_CAMERA      0x54
#define EEPROM_I2C_ADDR_SPD(_x_)    (0x50 + (_x_))

#define EEPROM_CAMERA_SERIAL_OFFSET 0
#define EEPROM_CAMERA_SERIAL_LEN    32

static int
do_info(struct fpga *fpga, char *const argv[], int argc)
{
    int fd, err, slot, colorfd;
    char serial[EEPROM_CAMERA_SERIAL_LEN];

	if ((fd = open(EEPROM_I2C_BUS, O_RDWR)) < 0) {
		fprintf(stderr, "Failed to open i2c bus (%s)\n", strerror(errno));
	    return -1;
    }

    /* Print the serial number */
    err = i2c_eeprom_read16(fd, EEPROM_I2C_ADDR_CAMERA, EEPROM_CAMERA_SERIAL_OFFSET, serial, sizeof(serial));
    if (err < 0) {
        fprintf(stderr, "Failed to read serial number (%s)\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("FPGA Version: %d\n", fpga->reg[FPGA_VERSION]);
    printf("Camera Serial: %.*s\n", sizeof(serial), serial);
    colorfd = open(LUX1310_COLOR_DETECT, O_RDONLY);
    if (colorfd >= 0) {
        printf("Image Sensor: LUX1310 %s\n", gpio_read(colorfd) ? "Color" : "Monochome");
        close(colorfd);
    }

    /* Attempt to read the DDR3 SPD info for slots 0 and 1 */
    for (slot = 0; slot < 2; slot++) {
        struct ddr3_spd spd;
        char sz_readable[32];
        err = i2c_eeprom_read(fd, EEPROM_I2C_ADDR_SPD(slot), 0, &spd, sizeof(spd));
        if (err < 0) {
            continue;
        }
        if (!spd_size_readable(&spd, sz_readable, sizeof(sz_readable))) {
            printf("\nDetected Unsupported RAM in Slot %d:\n", slot);
        }
        else {
            printf("\nDetected %s RAM in Slot %d:\n", sz_readable, slot);
            spd_fprint(&spd, stdout);
        }
    } /* for */

    close(fd);    
    return 0;
} /* do_info */

/* The eeprom subcommand */
const struct cli_subcmd cli_cmd_info = {
    .name = "info",
    .desc = "Read camera information",
    .function = do_info,
};
