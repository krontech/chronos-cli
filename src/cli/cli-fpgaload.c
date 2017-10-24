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

#define FPGA_SPIDEV_PATH    "/dev/spidev3.0"

static int
do_fpgaload(struct fpga *fpga, char *const argv[], int argc)
{
    /* TODO: Support options */
	optind = 1;
    
    /* If no arguments were provided, then dump all registers. */
    if (argc <= 1) {
        fprintf(stderr, "Missing argument: FILE\n");
        return 1;
    }

    /* Program the FPGA bitstream. */
    if (fpga_load(FPGA_SPIDEV_PATH, argv[optind], stdout) < 0) {
        fprintf(stderr, "FPGA Programming failed.\n");
        return 1;
    }

    return 0;
} /* do_fpgaload */

/* The eeprom subcommand */
const struct cli_subcmd cli_cmd_fpgaload = {
    .name = "fpgaload",
    .desc = "Load the FPGA bitstream",
    .function = do_fpgaload,
};
