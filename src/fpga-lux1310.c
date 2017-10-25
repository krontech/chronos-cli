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
#include <unistd.h>
#include <endian.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <linux/types.h>
#include <linux/spi/spidev.h>

#include "fpga.h"
#include "fpga-lux1310.h"
#include "cli.h"
#include "cli-utils.h"

uint16_t
fpga_sci_read(struct fpga *fpga, uint8_t addr)
{
    uint16_t first;
    int i;

    /* Set RW, address and length. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = addr;
    fpga->sensor->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

    if (!first && (i != 0)) {
        fprintf(stderr, "fpga_sci_read: Read first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "fpga_sci_read: Read busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }

    usleep(1000);
    return fpga->sensor->sci_fifo_data;
} /* fpga_sci_read */

void
fpga_sci_write(struct fpga *fpga, uint8_t addr, uint16_t value)
{
    uint16_t first;
    int i;

    /* Clear RW, and setup the transfer and fill the FIFO */
    fpga->sensor->sci_control &= ~SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = addr;
    fpga->sensor->sci_datalen = 2;
    fpga->sensor->sci_fifo_addr = (value >> 8) & 0xff;
    fpga->sensor->sci_fifo_addr = (value >> 0) & 0xff;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }
    if (!first && (i != 0)) {
        fprintf(stderr, "fpga_sci_write: Write first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "fpga_sci_write: Busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }
} /* fpga_sci_write */

/* DAC Constants for programming the image sensor analog voltage rails. */
#define LUX1310_DAC_VDR3_VOLTAGE    0
#define LUX1310_DAC_VABL_VOLTAGE    1
#define LUX1310_DAC_VDR1_VOLTAGE    2
#define LUX1310_DAC_VDR2_VOLTAGE    3
#define LUX1310_DAC_VRSTB_VOLTAGE   4
#define LUX1310_DAC_VRSTH_VOLTAGE   5
#define LUX1310_DAC_VRSTL_VOLTAGE   6
#define LUX1310_DAC_VRST_VOLTAGE    7

#define LUX1310_DAC_MODE_AUTOUPDATE 9

#define LUX1310_DAC_FS		        4095.0
#define LUX1310_DAC_VREF	        3.3
#define LUX1310_VDR3_SCALE          (LUX1310_DAC_FS / LUX1310_DAC_VREF)
#define LUX1310_VABL_SCALE          (LUX1310_DAC_FS / LUX1310_DAC_VREF * (10.0 + 23.2) / 10.0)
#define LUX1310_VDR1_SCALE          (LUX1310_DAC_FS / LUX1310_DAC_VREF)
#define LUX1310_VDR2_SCALE          (LUX1310_DAC_FS / LUX1310_DAC_VREF)
#define LUX1310_VRSTB_SCALE         (LUX1310_DAC_FS / LUX1310_DAC_VREF)
#define LUX1310_VRSTH_SCALE         (LUX1310_DAC_FS / LUX1310_DAC_VREF * 49.9 / (49.9 + 10.0))
#define LUX1310_VRSTL_SCALE         (LUX1310_DAC_FS / LUX1310_DAC_VREF * (10.0 + 23.2) / 10.0)
#define LUX1310_VRST_SCALE          (LUX1310_DAC_FS / LUX1310_DAC_VREF * 49.9 / (49.9 + 10.0))

/* Write a voltage level to the DAC */
static int
lux1310_set_voltage(struct fpga *fpga, int spifd, int channel, float value)
{   
    uint16_t reg = htobe16((((channel & 0x7) << 12) | 0x0fff));
    int err; 

    gpio_write(fpga->gpio.dac_cs, 0);
    err = write(spifd, &reg, sizeof(reg));
    gpio_write(fpga->gpio.dac_cs, 1);
    return err;
} /* lux1310_set_voltage */

int
lux1310_init(struct fpga *fpga, const char *spidev)
{
    int err;
    uint32_t hz = 1000000;
    uint8_t mode = 1;
    uint8_t wordsz = 16;
    uint16_t dacmode = htobe16(LUX1310_DAC_MODE_AUTOUPDATE << 12);
    int spifd = open(spidev, O_RDWR);
    if (spifd < 0) {
        fprintf(stderr, "Failed to open spidev device: %s\n", strerror(errno));
        return -1;
    }
	if (ioctl(spifd, SPI_IOC_WR_MODE, &mode) < 0) {
        fprintf(stderr, "Failed to set SPI write mode: %s\n", strerror(errno));
        close(spifd);
        return  -1;
    }
	if (ioctl(spifd, SPI_IOC_WR_BITS_PER_WORD, &wordsz) < 0) {
        fprintf(stderr, "Failed to set SPI word size: %s\n", strerror(errno));
        close(spifd);
        return  -1;
    }
    if (ioctl(spifd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
        fprintf(stderr, "Failed to set SPI clock speed: %s\n", strerror(errno));
        close(spifd);
        return  -1;
    }

    /* Setup the DAC to automatically update values on write. */
    gpio_write(fpga->gpio.dac_cs, 0);
    err = write(spifd, &dacmode, sizeof(dacmode));
    gpio_write(fpga->gpio.dac_cs, 1);
    if (err < 0) {
        fprintf(stderr, "Failed to set DAC update mode: %s\n", strerror(errno));
        close(spifd);
        return err;
    }
    /* Write the default voltage settings and reset the LUX1310 */
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VABL_VOLTAGE, 0.3);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VRSTB_VOLTAGE, 2.7);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VRST_VOLTAGE, 3.3);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VRSTL_VOLTAGE, 0.7);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VRSTH_VOLTAGE, 3.6);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VDR1_VOLTAGE, 2.5);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VDR2_VOLTAGE, 2.0);
    lux1310_set_voltage(fpga, spifd, LUX1310_DAC_VDR3_VOLTAGE, 1.5);

    /* Wait for the voltage leveles to settle and strobe the reset low. */
    usleep(10000);
    fpga->sensor->control |= IMAGE_SENSOR_RESET_MASK;
    usleep(100);
    fpga->sensor->control &= ~IMAGE_SENSOR_RESET_MASK;
    usleep(1000);

    /* Reset the SCI registers to default. */
    fpga_sci_write(fpga, LUX1310_SCI_SRESET_B >> LUX1310_SCI_REG_ADDR, 0);
    return 0;
} /* lux1310_init */
