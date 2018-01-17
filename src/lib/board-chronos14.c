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
#include "ioport.h"

/* Hardware assets for the Chronos 1.4 */
const struct ioport board_chronos14_ioports[] = {
    { .name = "ddr3-i2c",       .value = "/dev/i2c-0" },
    { .name = "eeprom-i2c",     .value = "/dev/i2c-1" },
    { .name = "lux1310-spidev", .value = "/dev/spidev3.0" },
    { .name = "lux1310-dac-cs", .value = "/sys/class/gpio/gpio33/value" },
    { .name = "lux1310-color",  .value = "/sys/class/gpio/gpio66/value" },
    { .name = "encoder-a",      .value = "/sys/class/gpio/gpio20/value" },
    { .name = "encoder-b",      .value = "/sys/class/gpio/gpio26/value" },
    { .name = "encoder-sw",     .value = "/sys/class/gpio/gpio27/value" },
    { .name = "shutter-sw",     .value = "/sys/class/gpio/gpio66/value" },
    { .name = "record-led.0",   .value = "/sys/class/gpio/gpio41/value" },
    { .name = "record-led.1",   .value = "/sys/class/gpio/gpio25/value" },
    { .name = "trigger-pin",    .value = "/sys/class/gpio/gpio127/value" },
    { .name = "frame-irq",      .value = "/sys/class/gpio/gpio51/value" },
    /* FPGA Programming Pins */
    { .name = "ecp5-spidev",    .value = "/dev/spidev3.0" },
    { .name = "ecp5-progn",     .value = "/sys/class/gpio/gpio47/value" },
    { .name = "ecp5-init",      .value = "/sys/class/gpio/gpio45/value" },
    { .name = "ecp5-done",      .value = "/sys/class/gpio/gpio52/value" },
    { .name = "ecp5-cs",        .value = "/sys/class/gpio/gpio58/value" },
    { .name = "ecp5-holdn",     .value = "/sys/class/gpio/gpio58/value" },
    /* List Termination */
    { 0, 0 }
};
