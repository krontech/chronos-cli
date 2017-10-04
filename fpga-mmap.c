/* Copyright 2017 Kron Technologies. All Rights Reserved. */
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
	volatile struct gpmc *gpmc;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "Unable to open /dev/mem: %s\n", strerror(errno));
		free(fpga);
		return NULL;
	}

	gpmc = mmap(0, 16 * SIZE_MB, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPMC_BASE);
	if (gpmc == MAP_FAILED) {
		close(fd);
		fprintf(stderr, "mmap failed for GPMC register space: %s\n", strerror(errno));
		return NULL;
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
						((GPMC_RANGE_BASE + GPMC_REGISTER_OFFSET) >> 24);			/*BASEADDRESS*/

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
							((GPMC_RANGE_BASE + GPMC_RAM_OFFSET) >> 24);			/*BASEADDRESS*/

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

	/*-------------------------------------------
	 * Set up memory mapping to the FPGA
	 *-------------------------------------------
	 */
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

#if 0
void GPMC::setTimeoutEnable(bool timeoutEnable)
{
	GPMC_TIMEOUT_CONTROL =	0x1FF << 4 |	/*TIMEOUTSTARTVALUE*/
							(timeoutEnable ? 1 : 0);				/*TIMEOUTENABLE*/
}
#endif
