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
#include "cli-utils.h"

extern int optind;

static int
do_led(struct fpga *fpga, char *const argv[], int argc)
{
    const char *val;
	optind = 1;

    /* If no arguments were provided, then dump all registers. */
    if (argc <= 1) {
        fprintf(stderr, "Missing argument: STATE\n");
        return 1;
    }

    /* Try to parse a boolean state... */
    val = argv[optind];
    if ((strcasecmp(val, "on") == 0) || (strcasecmp(val, "true") == 0)) {
        gpio_write(fpga->gpio.led_front, 1);
        gpio_write(fpga->gpio.led_back, 1);
    }
    else if ((strcasecmp(val, "off") == 0) || (strcasecmp(val, "false") == 0)) {
        gpio_write(fpga->gpio.led_front, 0);
        gpio_write(fpga->gpio.led_back, 0);
    }
    else {
        unsigned long x;
        char *end;
        x = (strtoul(val, &end, 0) != 0);
        if (*end != '\0') {
            fprintf(stderr, "Malformed STATE: \'%s\'\n", val);
            return 1;
        }
        gpio_write(fpga->gpio.led_front, x);
        gpio_write(fpga->gpio.led_back, x);
    }
    return 0;
} /* do_info */

/* The eeprom subcommand */
const struct cli_subcmd cli_cmd_led = {
    .name = "led",
    .desc = "Enable or disable the record LED",
    .function = do_led,
};
