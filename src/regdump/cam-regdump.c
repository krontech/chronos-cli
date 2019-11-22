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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "regs.h"
#include "fpga.h"
#include "utils.h"

/* Supported Luxima chip identifiers */
#define LUX1310_CHIP_ID 0xDA
#define LUX2100_CHIP_ID 0x28

static void
print_reg_group(FILE *fp, const struct reggroup *group, struct fpga *fpga)
{
    const char *header = "\t%-6s  %-10s  %24s  %s\n";
    const char *format = "\t0x%04x  0x%08x  %24s  0x%x\n";
    int i;

    if (group->name) fprintf(fp, "\n%s Registers:\n", group->name);
    fprintf(fp, header, "ADDR", "MASK", "NAME", "VALUE");

    if (group->setup) {
        group->setup(group, fpga);
    }

    for (i = 0; group->rtab[i].name; i++) {
        const struct regdef *reg = &group->rtab[i];
        unsigned long value = reg->reg_read(reg, fpga);
        uint32_t mask = (reg->mask) ? (reg->mask) : UINT32_MAX;
        fprintf(fp, format, reg->offset, mask, reg->name, getbits(value, mask));
    } /* for */
    
    if (group->cleanup) {
        group->cleanup(group, fpga);
    }
} /* print_reg_group */

int
main(int argc, char *const argv[])
{
    unsigned int chipid;
    struct fpga *fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA register space: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Pretty Print the registers. */
    print_reg_group(stdout, &sensor_registers, fpga);
    print_reg_group(stdout, &seq_registers, fpga);
    print_reg_group(stdout, &trigger_registers, fpga);
    print_reg_group(stdout, &display_registers, fpga);
    print_reg_group(stdout, &config_registers, fpga);
    print_reg_group(stdout, &vram_registers, fpga);
    print_reg_group(stdout, &seqpgm_registers, fpga);
    print_reg_group(stdout, &overlay_registers, fpga);
    print_reg_group(stdout, &imager_registers, fpga);
    print_reg_group(stdout, &zebra_registers, fpga);

    /* Read the Luxima chip ID. */
    chipid = sci_read_chipid(fpga);
    if (chipid == LUX1310_CHIP_ID) {
        print_reg_group(stdout, &lux1310_registers, fpga);
    }
    else if (chipid == LUX2100_CHIP_ID) {
        print_reg_group(stdout, &lux2100_sensor_registers, fpga);
        print_reg_group(stdout, &lux2100_datapath_registers, fpga);
    }
    else {
        print_reg_group(stdout, &luxima_chipid_registers, fpga);
    }

    fpga_close(fpga);
    return 0;
} /* main */
