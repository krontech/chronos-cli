/****************************************************************************
 *  Copyright (C) 2018 Kron Technologies Inc <http://www.krontech.ca>.      *
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

#include <stdlib.h>

#include "regs.h"
#include "fpga.h"
#include "utils.h"

unsigned long
sci_reg_read(const struct regdef *reg, struct fpga *fpga)
{
    uint16_t first;
    int i;

    /* Set RW, address and length. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = reg->offset;
    fpga->sensor->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

    if (!first && (i != 0)) {
        fprintf(stderr, "sci_reg_read: Read first busy was missed, address: 0x%02x\n", reg->offset);
    }
    else if (i == 0) {
        fprintf(stderr, "sci_reg_read: Read busy not detected, something probably wrong, address: 0x%02x\n", reg->offset);
    }

    usleep(1000);
    return fpga->sensor->sci_fifo_read;
}

int
sci_reg_write(const struct regdef *reg, struct fpga *fpga, unsigned long val)
{
    uint16_t first;
    int i;

    /* Clear RW, reset then set address and length. */
    fpga->sensor->sci_control = SENSOR_SCI_CONTROL_RESET_MASK;
    fpga->sensor->sci_address = reg->offset;
    fpga->sensor->sci_datalen = 2;

    /* Load the FIFO */
    fpga->sensor->sci_fifo_write = (val >> 8) & 0xff;
    fpga->sensor->sci_fifo_write = (val >> 0) & 0xff;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control = SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

    if (!first && (i != 0)) {
        fprintf(stderr, "sci_reg_write: Read first busy was missed, address: 0x%02x\n", reg->offset);
    }
    else if (i == 0) {
        fprintf(stderr, "sci_reg_write: Read busy not detected, something probably wrong, address: 0x%02x\n", reg->offset);
    }

    /* TODO: The C++ code does a readback. Do we care? */
	return 0;
}

static const struct regdef luxima_chipid_regdefs[] = {
    {
        .name = "chip_id",
        .offset = 0x00,
        .mask = 0xff00,
        .size = 2,
        .reg_read = sci_reg_read,
    },
    {
        .name = "rev_chip",
        .offset = 0x00,
        .mask = 0x00ff,
        .size = 2,
        .reg_read = sci_reg_read,
    },
    {NULL, 0, 0, 0}
};
const struct reggroup luxima_chipid_registers = {
    .name = "Unknown Sensor",
    .rtab = luxima_chipid_regdefs,
};

unsigned int
sci_read_chipid(struct fpga *fpga)
{
    const struct regdef *r = &luxima_chipid_regdefs[0];
    return getbits(sci_reg_read(r, fpga), r->mask);
}
