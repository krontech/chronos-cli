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

	/* Open GPIOs */
	fpga->gpio.enc_a = open(GPIO_ENCODER_A, O_RDONLY | O_NONBLOCK);
	fpga->gpio.enc_b = open(GPIO_ENCODER_B, O_RDONLY | O_NONBLOCK);
	fpga->gpio.enc_sw = open(GPIO_ENCODER_SWITCH, O_RDONLY);
	fpga->gpio.shutter = open(GPIO_SHUTTER_SWITCH, O_RDONLY);
	fpga->gpio.led_front = open(GPIO_RECORD_LED_FRONT, O_WRONLY);
	fpga->gpio.led_back = open(GPIO_RECORD_LED_BACK, O_WRONLY);
	fpga->gpio.frame_irq = open(GPIO_FRAME_IRQ, O_RDONLY | O_NONBLOCK);
	if ((fpga->gpio.enc_a < 0) || (fpga->gpio.enc_b < 0) || (fpga->gpio.enc_sw < 0) ||
		(fpga->gpio.shutter < 0) || (fpga->gpio.led_front < 0) || (fpga->gpio.led_back < 0) ||
		(fpga->gpio.frame_irq < 0)) {
		fprintf(stderr, "Failed to open GPIO devices: %s\n", strerror(errno));
		fpga_close(fpga);
		return NULL;
	}

	/* Setup structured access to FPGA registers. */
	fpga->sensor = (struct fpga_sensor *)(fpga->reg + SENSOR_CONTROL);
	fpga->seq = (struct fpga_seq *)(fpga->reg + SEQ_CONTROL);
	fpga->display = (struct fpga_display *)(fpga->reg + DISPLAY_CTL);
	fpga->cc_matrix = (uint32_t *)(fpga->reg + CCM_ADDR);
	fprintf(stderr, "%s: %p %p %p\n", __func__, fpga->sensor, fpga->seq, fpga->display);
	return fpga;
} /* fpga_open */

void
fpga_close(struct fpga *fpga)
{
	if (fpga) {
		if (fpga->gpio.enc_a >= 0) close(fpga->gpio.enc_a);
		if (fpga->gpio.enc_b >= 0) close(fpga->gpio.enc_b);
		if (fpga->gpio.enc_sw >= 0) close(fpga->gpio.enc_sw);
		if (fpga->gpio.shutter >= 0) close(fpga->gpio.shutter);
		if (fpga->gpio.led_front >= 0) close(fpga->gpio.led_front);
		if (fpga->gpio.led_back >= 0) close(fpga->gpio.led_back);
		if (fpga->gpio.frame_irq >= 0) close(fpga->gpio.frame_irq);
		if (fpga->reg != MAP_FAILED) munmap((void *)fpga->reg, 16 * SIZE_MB);
		if (fpga->ram != MAP_FAILED) munmap((void *)fpga->ram, 16 * SIZE_MB);
		close(fpga->fd);
		free(fpga);
	}
} /* fpga_close */
