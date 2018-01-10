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
#include <stdarg.h>
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

void
fpga_sci_writebuf(struct fpga *fpga, uint8_t addr, const void *data, size_t len)
{
    size_t i;
    const uint8_t *p;

    /* Clear RW, and setup the transfer and fill the FIFO */
    fpga->sensor->sci_control &= ~SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = addr;
    fpga->sensor->sci_datalen = len;
    for (i = 0; i < len; i++) {
        fpga->sensor->sci_fifo_addr = *p++;
    }

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    while((fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) != 0) { /*nop */ }
} /* fpga_sci_writebuf */

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

unsigned int
lux1310_read(struct fpga *fpga, unsigned int reg)
{
    unsigned int val = fpga_sci_read(fpga, reg >> LUX1310_SCI_REG_ADDR);
    return getbits(val, reg & LUX1310_SCI_REG_MASK);
} /* lux1310_read */

/* Perform a simple register write, which contains only one sub-field. */
void
lux1310_write(struct fpga *fpga, unsigned int reg, unsigned int val)
{
    fpga_sci_write(fpga, reg >> LUX1310_SCI_REG_ADDR, setbits(val, reg & LUX1310_SCI_REG_MASK));
} /* lux1310_write */

/* Write multiple registers at the same address. */
void
lux1310_write_many(struct fpga *fpga, ...)
{
    unsigned int reg;
    unsigned int addr = 0;
    unsigned int val = 0;
    va_list ap;
    va_start(ap, fpga);
    while ((reg = va_arg(ap, unsigned int)) != 0) {
        addr = (reg >> LUX1310_SCI_REG_ADDR);
        val |= setbits(va_arg(ap, unsigned int), reg & LUX1310_SCI_REG_MASK);
    }
    va_end(ap);
    fpga_sci_write(fpga, addr, val);
} /* lux1310_write_many */

/* Program a wave table into the image sensor and configure the readout delays to match. */
void
lux1310_write_wavetab(struct fpga *fpga, const struct lux1310_wavetab *wave)
{
    lux1310_write(fpga, LUX1310_SCI_TIMING_EN, 0);
    lux1310_write(fpga, LUX1310_SCI_RDOUT_DLY, wave->read_delay);
    lux1310_write(fpga, LUX1310_SCI_WAVETAB_SIZE, wave->read_delay);
    fpga_sci_writebuf(fpga, 0x7F, wave->table, wave->len);
    fpga->reg[SENSOR_MAGIC_START_DELAY] = wave->start_delay;
} /* lux1310_write_wavetab */

static int
lux1310_auto_phase_cal(struct fpga *fpga)
{
    int data_correct;

    /* Enable the ADC training pattern. */
    lux1310_write(fpga, LUX1310_SCI_CUST_PAT, 0xfc0);
    lux1310_write(fpga, LUX1310_SCI_TST_PAT, 2);
    lux1310_write(fpga, LUX1310_SCI_PCLK_VBLANK, 0xfc0);
    lux1310_write_many(fpga,
            LUX1310_SCI_DAC_ILV, 1,
            LUX1310_SCI_MSB_FIRST_DATA, 0,
            LUX1310_SCI_TERMB_CLK, 1,
            LUX1310_SCI_TERMB_DATA, 1,
            LUX1310_SCI_DCLK_INV, 1,
            LUX1310_SCI_PCLK_INV, 0,
            0);
    
    /* Toggle the clock phase and wait for the FPGA to lock. */
	fpga->sensor->clk_phase = 0;
	fpga->sensor->clk_phase = 1;
    fpga->sensor->clk_phase = 0;
    /* TODO: Shouldn't there be a while loop here? */
    data_correct = fpga->sensor->data_correct;
    fprintf(stderr, "lux1310_data_correct: 0x%04x\n", data_correct);

    /* Return to normal mode. */
    lux1310_write(fpga, LUX1310_SCI_PCLK_VBLANK, 0xf00);
    lux1310_write(fpga, LUX1310_SCI_TST_PAT, 0);
    return 0;
} /* lux1310_auto_phase_cal */

int
lux1310_init(struct fpga *fpga, const char *spidev)
{
    int err;
    uint32_t hz = 1000000;
    uint8_t mode = 1;
    uint8_t wordsz = 16;
    uint16_t dacmode = htobe16(LUX1310_DAC_MODE_AUTOUPDATE << 12);
    uint16_t rev;
    int i;
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

    /* Disable integration */
    fpga->sensor->frame_period = 100 * 4000;
    fpga->sensor->int_time = 100 * 4100;

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

    /* Perform automatic phase calibration. */
    err = lux1310_auto_phase_cal(fpga);
    if (err != 0) {
        return err;
    }

    /* Set internal controls for performance fine-tuning */
    lux1310_write(fpga, LUX1310_SCI_LINE_VALID_DLY, 7);
    lux1310_write(fpga, LUX1310_SCI_STATE_IDLE_CTRL0, 0xe08e);
    lux1310_write(fpga, LUX1310_SCI_STATE_IDLE_CTRL1, 0xfc1f);
    lux1310_write(fpga, LUX1310_SCI_STATE_IDLE_CTRL2, 0x0003);
    lux1310_write(fpga, LUX1310_SCI_ADC_CLOCK_CTRL, 0x2202);
    lux1310_write_many(fpga,
            LUX1310_SCI_SEL_VCMI, 6,
            LUX1310_SCI_SEL_VCMO, 7,
            LUX1310_SCI_SEL_VCMP, 11,
            LUX1310_SCI_SEL_VCMN, 4,
            0);
    lux1310_write(fpga, LUX1310_SCI_INT_CLK_TIMING, 0x41f);

    /* Grab the sensor revision for further tuning. */
    rev = lux1310_read(fpga, LUX1310_SCI_REV_CHIP);
    fprintf(stderr, "configuring for LUX1310 silicon rev %d\n", rev);
    switch (rev) {
        case 2:
            fpga_sci_write(fpga, 0x5B, 0x307f);
            fpga_sci_write(fpga, 0x7B, 0x3007);
            break;

        case 1:
        default:
            fpga_sci_write(fpga, 0x5B, 0x301f);
            fpga_sci_write(fpga, 0x7B, 0x3001);
            break;
    } /* switch */

    /* Clear the ADC Offsets table, and then enable the ADC offset calibration. */
    for (i = 0; i < 16; i++) {
        lux1310_write(fpga, LUX1310_SCI_ADC_OS(i), 0);
    }
    lux1310_write(fpga, LUX1310_SCI_ADC_CAL_EN, 1);

    /* Program the gain. */
    lux1310_write(fpga, LUX1310_SCI_GAIN_SEL_SAMP, 0x7f);
    lux1310_write(fpga, LUX1310_SCI_GAIN_SEL_FB, 0x7f);
    lux1310_write(fpga, LUX1310_SCI_GAIN_BIT, 3);

    /* TODO: Program the wavetable. */
    
    /* Enable the sensor timing engine. */
    lux1310_write(fpga, LUX1310_SCI_TIMING_EN, 1);
    usleep(10000);
    fpga->sensor->frame_period = 100 * 4000;
    fpga->sensor->int_time = 100 * 3900;
    usleep(50000);

    /* TODO: Set a default the frame resolution and period and integration time. */

    return 0;
} /* lux1310_init */
