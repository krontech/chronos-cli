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

    if (group->name) fprintf(fp, "%s Registers:\n", group->name);
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
    fputs("\n", fp);
    
    if (group->cleanup) {
        group->cleanup(group, fpga);
    }
} /* print_reg_group */

const struct reggroup *available[] = {
    &sensor_registers,
    &seq_registers,
    &trigger_registers,
    &display_registers,
    &config_registers,
    &vram_registers,
    &seqpgm_registers,
    &overlay_registers,
    &imager_registers,
    &timing_registers,
    &zebra_registers,
    /* Sensor registers */
    &lux1310_registers,
    &lux2100_sensor_registers,
    &lux2100_datapath_registers,
};
const int num_groups = sizeof(available)/sizeof(available[0]);

static void
usage(int argc, char *const argv[])
{
    int i;

    printf("usage: %s [options]\n\n", argv[0]);
    printf("Dump the Chronos FPGA registers to standard outout.\n\n");
    printf("options:\n");
    printf("  --help         display this message and exit\n");
    
    /* Show the generated filter options */
    for (i = 0; i < num_groups; i++) {
        printf("  --%-12s display only the %s register group\n", available[i]->filter, available[i]->name);
    }
} 

int
main(int argc, char *const argv[])
{
    struct fpga *fpga;
    int luxdetect = 0;

    const char *shortopts = "h";
    unsigned long long mask = 0;
    struct option options[num_groups+2];
    int i;
    
    /* Build up the getopt options */
    for (i = 0; i < num_groups; i++) {
        options[i].name = available[i]->filter;
        options[i].has_arg = no_argument;
        options[i].flag = NULL;
        options[i].val = i;
    }
    /* Add the help option. */
    options[i].name = "help";
    options[i].has_arg = no_argument;
    options[i].flag = NULL;
    options[i].val = 'h';
    i++;

    /* Add the option terminator */
    options[i].name = NULL;
    options[i].has_arg = 0;
    options[i].flag = 0;
    options[i].val = 0;

    /* Parse the flags */
    optind = 1;
    while ((optind < argc) && (*argv[optind] == '-')) {
        int c = getopt_long(argc, argv, shortopts, options, NULL);
        if (c < 0) {
            /* End of options */
            break;
        }
        if (c == 'h') {
            usage(argc, argv);
            return 0;
        }
        if (c < 8 * sizeof(mask)) {
            mask |= (1 << c);
        }
    }

    /* Open the FPGA register mapping */
    fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA register space: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Pretty print the registers. */
    for (i = 0; i < num_groups; i++) {
        /* If specifically enabled - print it. */
        if (mask & (1 << i)) {
            print_reg_group(stdout, available[i], fpga);
        }
        /* If nothing is filtered, print everything. */
        if (!mask) {
            if (available[i]->detect) {
                if (!available[i]->detect(available[i], fpga)) continue;
            }
            if (memcmp(available[i]->name, "LUX", 3) == 0) luxdetect++;
            print_reg_group(stdout, available[i], fpga);
        }
    }

    /* Special case - if chipid detection failed, print something. */
    if ((mask == 0) && (luxdetect == 0)) {
        print_reg_group(stdout, &luxima_chipid_registers, fpga);
    }

    fpga_close(fpga);
    return 0;
} /* main */
