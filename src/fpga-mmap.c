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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/mman.h>

#include <fpga.h>
#include <fpga-gpmc.h>

#define SIZE_MB (1024 * 1024)

struct fpga *
fpga_open(void)
{
	int fd;
	struct fpga *fpga;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "Unable to open /dev/mem: %s\n", strerror(errno));
		free(fpga);
		return NULL;
	}

	fpga = malloc(sizeof(struct fpga));
	if (!fpga) {
		fprintf(stderr, "Memory allocation failed: %s\n", strerror(errno));
		return NULL;
	}
	fpga->fd = fd;
	fpga->reg = (volatile uint16_t *)mmap(0, 16 * SIZE_MB, PROT_READ | PROT_WRITE, MAP_SHARED, fpga->fd, GPMC_RANGE_BASE + GPMC_REGISTER_OFFSET);
	if (fpga->reg == MAP_FAILED) {
		fprintf(stderr, "Failed to map FPGA registers: %s\n", strerror(errno));
		close(fpga->fd);
		free(fpga);
		return NULL;
	}
	fpga->ram = (volatile uint16_t *)mmap(0, 16 * SIZE_MB, PROT_READ | PROT_WRITE, MAP_SHARED, fpga->fd, GPMC_RANGE_BASE + GPMC_RAM_OFFSET);
	if (fpga->ram == MAP_FAILED) {
		fprintf(stderr, "Failed to map FPGA memory: %s\n", strerror(errno));
		munmap((void *)fpga->reg, 16 * SIZE_MB);
		close(fpga->fd);
		free(fpga);
		return NULL;
	}

	/* Setup structured access to FPGA internals. */
	fpga->sensor = (struct fpga_sensor *)(fpga->reg + SENSOR_CONTROL);
	fpga->seq = (struct fpga_seq *)(fpga->reg + SEQ_CTL);
	fpga->display = (struct fpga_seq *)(fpga->reg + DISPLAY_CTL);
	return fpga;
} /* fpga_open */

void
fpga_close(struct fpga *fpga)
{
	if (fpga) {
		munmap((void *)fpga->reg, 16 * SIZE_MB);
		munmap((void *)fpga->ram, 16 * SIZE_MB);
		close(fpga->fd);
		free(fpga);
	}
} /* fpga_close */
