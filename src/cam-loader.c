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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fpga.h>
#include <fpga-gpmc.h>

#define DEFAULT_IOPORTS_FILE    "/etc/cam-board.json"

#define SIZE_MB (1024 * 1024)

static int
gpmc_setup(void)
{
    volatile struct gpmc *gpmc;
    int fd;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Unable to open /dev/mem: %s\n", strerror(errno));
        return -1;
    }

    gpmc = mmap(0, 16 * SIZE_MB, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPMC_BASE);
    if (gpmc == MAP_FAILED) {
        fprintf(stderr, "mmap failed for GPMC register space: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Reset GPMC */
    gpmc->sysconfig |= GPMC_SYSCONFIG_SOFTRESET;
    while (!(gpmc->sysstatus & GPMC_SYSSTATUS_RESETDONE)) { /*nop */ }

    /* Set wait pins to active high */
    gpmc->config |= (GPMC_CONFIG_WAIT1_POLARITY | GPMC_CONFIG_WAIT0_POLARITY);
    gpmc->timeout_control = GPMC_TIMEOUT_START_VALUE(0x1ff);

    /*-------------------------------------------
     * Configure Chip Select 0
     *-------------------------------------------
     */
    gpmc->cs[0].config7 = 0;

    /* Setup CS config */
    gpmc->cs[0].config1 = GPMC_CONFIG_CS_READ_TYPE |
                            GPMC_CONFIG_CS_WRITE_TYPE |
                            GPMC_CONFIG_CS_CLK_ACTIVATION(0) |
                            GPMC_COFNIG_CS_PAGE_LENGTH_4 |
                            GPMC_CONFIG_CS_WAIT_TIME_ZERO |
                            GPMC_CONFIG_CS_WAIT_PIN_WAIT0 |
                            GPMC_CONFIG_CS_DEVICE_16BIT |
                            GPMC_CONFIG_CS_DEVICE_NOR |
                            GPMC_CONFIG_CS_MUX_ADDR_DATA_DEVICE |
                            GPMC_CONFIG_CS_FCLK_NO_DIV;

    gpmc->cs[0].config7 = GPMC_CONFIG_MASK_ADDRESS_16MB |
                        ((GPMC_RANGE_BASE + GPMC_REGISTER_OFFSET) >> 24);            /*BASEADDRESS*/

    //Setup timings
    gpmc->cs[0].config2 = GPMC_CONFIG_CS_WR_OFF_TIME(11) |
                            GPMC_CONFIG_CS_RD_OFF_TIME(12) |
                            GPMC_CONFIG_CS_ON_TIME(1);

    gpmc->cs[0].config3 = GPMC_CONFIG_ADV_AAD_WR_OFF_TIME(0) |
                            GPMC_CONFIG_ADV_AAD_RD_OFF_TIME(0) |
                            GPMC_CONFIG_ADV_WR_OFF_TIME(6) |
                            GPMC_CONFIG_ADV_RD_OFF_TIME(6) |
                            GPMC_CONFIG_ADV_AAD_ON_TIME(0) |
                            GPMC_CONFIG_ADV_ON_TIME(2);

    gpmc->cs[0].config4 = GPMC_CONFIG_WE_OFF_TIME(10) |
                            GPMC_CONFIG_WE_ON_TIME(7) |
                            GPMC_CONFIG_OE_AAD_OFF_TIME(0) |
                            GPMC_CONFIG_OE_OFF_TIME(11) |
                            GPMC_CONFIG_OE_AAD_ON_TIME(0) |
                            GPMC_CONFIG_OE_ON_TIME(7);

    gpmc->cs[0].config5 = GPMC_CONFIG_PAGE_BURST_TIME(0) |
                            GPMC_CONFIG_RD_ACCESS_TIME(10) |
                            GPMC_CONFIG_WR_CYCLE_TIME(12) |
                            GPMC_CONFIG_RD_CYCLE_TIME(12);

    gpmc->cs[0].config6 = GPMC_CONFIG_WR_ACCESS_TIME(6) |
                            GPMC_CONFIG_WR_DATA_MUX(7) |
                            GPMC_CONFIG_CYCLE_DELAY(1) |
                            GPMC_CONFIG_CYCLE_SAME_CS |
                            GPMC_CONFIG_CYCLE_DIFF_CS |
                            GPMC_COFNIG_BUS_TURNAROUND(1);

    gpmc->cs[0].config7 |= GPMC_CONFIG_CS_VALID;

    /*-------------------------------------------
     * Configure Chip Select 1
     *-------------------------------------------
     */
    gpmc->cs[1].config7 = 0;

    gpmc->cs[1].config1 = GPMC_CONFIG_CS_CLK_ACTIVATION(0) |
                            GPMC_COFNIG_CS_PAGE_LENGTH_4 |
                            GPMC_CONFIG_CS_WAIT_READ |
                            GPMC_CONFIG_CS_WAIT_WRITE |
                            GPMC_CONFIG_CS_WAIT_TIME_ZERO |
                            GPMC_CONFIG_CS_WAIT_PIN_WAIT0 |
                            GPMC_CONFIG_CS_DEVICE_16BIT |
                            GPMC_CONFIG_CS_DEVICE_NOR |
                            GPMC_CONFIG_CS_MUX_AAD_PROTO |
                            GPMC_CONFIG_CS_FCLK_NO_DIV;

    gpmc->cs[1].config7 = GPMC_CONFIG_MASK_ADDRESS_16MB | 
                            ((GPMC_RANGE_BASE + GPMC_RAM_OFFSET) >> 24);            /*BASEADDRESS*/

    //Setup timings
    gpmc->cs[1].config2 = GPMC_CONFIG_CS_WR_OFF_TIME(18) |
                            GPMC_CONFIG_CS_RD_OFF_TIME(18) |
                            GPMC_CONFIG_CS_ON_TIME(1);

    gpmc->cs[1].config3 = GPMC_CONFIG_ADV_AAD_WR_OFF_TIME(2) |
                            GPMC_CONFIG_ADV_AAD_RD_OFF_TIME(2) |
                            GPMC_CONFIG_ADV_WR_OFF_TIME(6) |
                            GPMC_CONFIG_ADV_RD_OFF_TIME(6) |
                            GPMC_CONFIG_ADV_AAD_ON_TIME(1) |
                            GPMC_CONFIG_ADV_ON_TIME(4);

    gpmc->cs[1].config4 = GPMC_CONFIG_WE_OFF_TIME(8) |
                            GPMC_CONFIG_WE_ON_TIME(6) |
                            GPMC_CONFIG_OE_AAD_OFF_TIME(3) |
                            GPMC_CONFIG_OE_OFF_TIME(18) |
                            GPMC_CONFIG_OE_AAD_ON_TIME(0) |
                            GPMC_CONFIG_OE_ON_TIME(7);

    gpmc->cs[1].config5 = GPMC_CONFIG_PAGE_BURST_TIME(0) |
                            GPMC_CONFIG_RD_ACCESS_TIME(17) |
                            GPMC_CONFIG_WR_CYCLE_TIME(19) |
                            GPMC_CONFIG_RD_CYCLE_TIME(19);

    gpmc->cs[1].config6 = GPMC_CONFIG_WR_ACCESS_TIME(12) |
                            GPMC_CONFIG_WR_DATA_MUX(7) |
                            GPMC_CONFIG_CYCLE_DELAY(1) |
                            GPMC_CONFIG_CYCLE_SAME_CS |
                            GPMC_CONFIG_CYCLE_DIFF_CS |
                            GPMC_COFNIG_BUS_TURNAROUND(1);

    gpmc->cs[1].config7 |= GPMC_CONFIG_CS_VALID;

    /* Cleanup GPMC registers memory mapping */
    munmap((void *)gpmc, 16 * SIZE_MB);
    return 0;
} /* gpmc_setup */

extern int optind;

static void
usage(int argc, char *const argv[])
{
    printf("usage: %s [options] FILE\n\n", argv[0]);
    printf("Program the Chronos FPGA with a bitstream image from FILE.\n\n");

    printf("options:\n");
    printf("  -b, --board PATH  Load board definitions from FILE (default: %s)\n", DEFAULT_IOPORTS_FILE);
    printf("  -u, --unload      Power down and assert FPGA reset instead of programming.\n");
    printf("  --help            display this message and exit\n");
} /* usage */

int
main(int argc, char *const argv[])
{
    const char *bitstream;
    const char *boardfile = DEFAULT_IOPORTS_FILE;
    const char *spidev;
    struct ioport *iops;
    struct fpga *fpga;
    uint16_t ver_major, ver_minor;
    int ret;
    int unload = 0;
    const char *shortopts = "b:uh";
    const struct option options[] = {
        {"board",   required_argument,  NULL, 'b'},
        {"unload",  no_argument,        NULL, 'u'},
        {"help",    no_argument,        NULL, 'h'},
        {0, 0, 0, 0}
    };

    /*
     * Parse command line options until we encounter the first positional
     * argument, which denodes the subcommand we want to execute. This
     * would be to handle stuff relating to how we connect to the FPGA.
     */
    optind = 1;
    while ((optind < argc) && (*argv[optind] == '-')) {
        int c = getopt_long(argc, argv, shortopts, options, NULL);
        if (c < 0) {
            /* End of options */
            break;
        }
        switch (c) {
            case 'b':
                boardfile = optarg;
                break;
            
            case 'u':
                unload = 1;
                break;

            case 'h':
                usage(argc, argv);
                return 0;

            case 'v':
                /* TODO: Implement Help and Version output. */
                break;
        }
    }
    /* Load the board definitions file. */
    iops = ioport_load_json(boardfile);
    if (!iops) {
        fprintf(stderr, "Failed to load board definitions from %s\n", boardfile);
        return 1;
    }

    /* No arguments required for the unload option. */
    if (unload) {
        return fpga_unload(iops);
    }

    /* Otherwise, Require one positional argument for the bitstream file. */
    if (optind >= argc) {
        fprintf(stderr, "Missing argument: FILE\n\n");
        usage(argc, argv);
        return 1;
    }
    bitstream = argv[optind++];

    /* Setup the FPGA and the GPMC memory controller. */
    ret = fpga_load(iops, bitstream, stderr);
    if (ret != 0) {
        return 1;
    }
    ret = gpmc_setup();
    if (ret != 0) {
        return 1;
    }

    /* Reset the FPGA and load the memory space.  */
    fpga = fpga_open();
    if (fpga == NULL) {
        return 1;
    }
    fpga->config->sys_reset = 1;
    usleep(200000);
    ver_major = fpga->config->version;
    ver_minor = fpga->config->subver;
    fprintf(stdout, "Loaded FPGA bistream version: %d.%d\n", ver_major, ver_minor);
    fpga_close(fpga);
    return 0;
} /* main */
