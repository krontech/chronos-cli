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
#include "fpga-sensor.h"
#include "lux1310.h"
#include "utils.h"
#include "ioport.h"

#define LUX1310_CLOCK_RATE      90000000
#define LUX1310_MIN_EXPOSURE    1000
#define LUX1310_MAGIC_ABN_DELAY 26

struct lux1310_private_data {
    struct image_sensor sensor;
    volatile struct fpga_sensor *reg;
    int spifd;
    int daccs;

    /* Internal knobs */
    unsigned int start_delay;
};

static uint16_t
lux1310_sci_read(struct fpga *fpga, uint8_t addr)
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
        fprintf(stderr, "lux1310_sci_read: Read first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "lux1310_sci_read: Read busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }

    usleep(1000);
    return fpga->sensor->sci_fifo_data;
} /* lux1310_sci_read */

static void
lux1310_sci_write(struct fpga *fpga, uint8_t addr, uint16_t value)
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
        fprintf(stderr, "lux1310_sci_write: Write first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "lux1310_sci_write: Busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }
} /* lux1310_sci_write */

static void
lux1310_sci_writebuf(struct fpga *fpga, uint8_t addr, const void *buf, size_t len)
{
    size_t i;
    const uint8_t *p = buf;

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
} /* lux1310_sci_writebuf */

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
lux1310_set_voltage(struct lux1310_private_data *data, int channel, float value)
{   
    uint16_t reg = htobe16((((channel & 0x7) << 12) | 0x0fff));
    int err; 

    gpio_write(data->daccs, 0);
    err = write(data->spifd, &reg, sizeof(reg));
    gpio_write(data->daccs, 1);
    return err;
} /* lux1310_set_voltage */

static unsigned int
lux1310_read(struct lux1310_private_data *data, unsigned int reg)
{
    unsigned int val = lux1310_sci_read(data->sensor.fpga, reg >> LUX1310_SCI_REG_ADDR);
    return getbits(val, reg & LUX1310_SCI_REG_MASK);
} /* lux1310_read */

/* Perform a simple register write, which contains only one sub-field. */
static void
lux1310_write(struct lux1310_private_data *data, unsigned int reg, unsigned int val)
{
    lux1310_sci_write(data->sensor.fpga, reg >> LUX1310_SCI_REG_ADDR, setbits(val, reg & LUX1310_SCI_REG_MASK));
} /* lux1310_write */

/* Write multiple registers at the same address. */
static void
lux1310_write_many(struct lux1310_private_data *data, ...)
{
    unsigned int reg;
    unsigned int addr = 0;
    unsigned int val = 0;
    va_list ap;
    va_start(ap, data);
    while ((reg = va_arg(ap, unsigned int)) != 0) {
        addr = (reg >> LUX1310_SCI_REG_ADDR);
        val |= setbits(va_arg(ap, unsigned int), reg & LUX1310_SCI_REG_MASK);
    }
    va_end(ap);
    lux1310_sci_write(data->sensor.fpga, addr, val);
} /* lux1310_write_many */

/* Program a wave table into the image sensor and configure the readout delays to match. */
static void
lux1310_write_wavetab(struct lux1310_private_data *data, const struct lux1310_wavetab *wave)
{
    lux1310_write(data, LUX1310_SCI_TIMING_EN, 0);
    lux1310_write(data, LUX1310_SCI_RDOUT_DLY, wave->read_delay);
    lux1310_write(data, LUX1310_SCI_WAVETAB_SIZE, wave->read_delay);
    lux1310_sci_writebuf(data->sensor.fpga, 0x7F, wave->table, wave->len);
    data->sensor.fpga->reg[SENSOR_MAGIC_START_DELAY] = wave->start_delay;
} /* lux1310_write_wavetab */

static int
lux1310_auto_phase_cal(struct lux1310_private_data *data)
{
    int data_correct;

    /* Enable the ADC training pattern. */
    lux1310_write(data, LUX1310_SCI_CUST_PAT, 0xfc0);
    lux1310_write(data, LUX1310_SCI_TST_PAT, 2);
    lux1310_write(data, LUX1310_SCI_PCLK_VBLANK, 0xfc0);
    lux1310_write_many(data,
            LUX1310_SCI_DAC_ILV, 1,
            LUX1310_SCI_MSB_FIRST_DATA, 0,
            LUX1310_SCI_TERMB_CLK, 1,
            LUX1310_SCI_TERMB_DATA, 1,
            LUX1310_SCI_DCLK_INV, 1,
            LUX1310_SCI_PCLK_INV, 0,
            0);
    
    /* Toggle the clock phase and wait for the FPGA to lock. */
	data->reg->clk_phase = 0;
	data->reg->clk_phase = 1;
    data->reg->clk_phase = 0;
    /* TODO: Shouldn't there be a while loop here? */
    data_correct = data->reg->data_correct;
    fprintf(stderr, "lux1310_data_correct: 0x%04x\n", data_correct);

    /* Return to normal mode. */
    lux1310_write(data, LUX1310_SCI_PCLK_VBLANK, 0xf00);
    lux1310_write(data, LUX1310_SCI_TST_PAT, 0);
    return 0;
} /* lux1310_auto_phase_cal */

#define LUX1310_HRES_INCREMENT 16

/* Configure the sensor to a given resolution, or return -1 on error. */
static int
lux1310_set_resolution(struct image_sensor *s, unsigned long hres, unsigned long vres, unsigned long hoff, unsigned long voff)
{
    struct lux1310_private_data *data = CONTAINER_OF(s, struct lux1310_private_data, sensor);

	uint32_t h_start = hoff / LUX1310_HRES_INCREMENT;
	uint32_t h_width = hres / LUX1310_HRES_INCREMENT;
    lux1310_write(data, LUX1310_SCI_X_START, 0x20 + h_start * LUX1310_HRES_INCREMENT);
	lux1310_write(data, LUX1310_SCI_X_END, 0x20 + (h_start + h_width) * LUX1310_HRES_INCREMENT - 1);
	lux1310_write(data, LUX1310_SCI_Y_START, voff);
	lux1310_write(data, LUX1310_SCI_Y_END, voff + vres);
    return 0;
}

/* Round the exposure timing to the nearest value the sensor can accept. */
static unsigned long long
lux1310_round_exposure(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec)
{
    if (nsec < LUX1310_MIN_EXPOSURE) {
        return LUX1310_MIN_EXPOSURE;
    }
    /* Round up to the nearest clock tick. */
    nsec += ((1000000000 / LUX1310_CLOCK_RATE) / 2);
    return nsec - (nsec % (1000000000 / LUX1310_CLOCK_RATE));
}

static unsigned long long
lux1310_round_period(struct image_sensor *sensor, unsigned long hres, unsigned long vres, unsigned long long nsec)
{
    const unsigned long t_hblank = 2;
    const unsigned long t_tx = 25;
    const unsigned long t_fovf = 50;
    const unsigned long t_fovb = 50; /* Duration between PRSTN falling and TXN falling (I think) */
    
    /* Sum up the minimum number of clocks to read a frame at this resolution */
    unsigned long t_read = (hres / LUX1310_HRES_INCREMENT);
    unsigned long t_wavetable = 80; /* TODO: Wavetable size from where? */
    unsigned long t_row = max(t_read + t_hblank, t_wavetable + 3);
    unsigned long t_frame = t_row * vres + t_tx + t_fovf + t_fovb;

    return max((t_frame * 1000000000) / LUX1310_CLOCK_RATE, nsec);
}

static const struct image_sensor_ops lux1310_ops = {
    .round_exposure = lux1310_round_exposure,
    .round_period = lux1310_round_period,
    .set_resolution = lux1310_set_resolution,
};

struct image_sensor *
lux1310_init(struct fpga *fpga, const struct ioport *iops)
{
    struct lux1310_private_data *data;
    uint32_t hz = 1000000;
    uint8_t mode = 1;
    uint8_t wordsz = 16;
    uint16_t dacmode = htobe16(LUX1310_DAC_MODE_AUTOUPDATE << 12);
    uint16_t rev;
    int color;
    int err;
    int i;

    data = malloc(sizeof(struct lux1310_private_data));
    if (!data) {
        return NULL;
    }
    data->spifd = open(ioport_find_by_name(iops, "lux1310-spidev"), O_RDWR);
    if (data->spifd < 0) {
        fprintf(stderr, "Failed to open spidev device: %s\n", strerror(errno));
        free(data);
        return NULL;
    }
    data->daccs = open(ioport_find_by_name(iops, "lux1310-dac-cs"), O_WRONLY);
    if (data->daccs < 0) {
        fprintf(stderr, "Failed to open DAC chipselect: %s\n", strerror(errno));
        close(data->spifd);
        free(data);
        return NULL;
    }
    
	if (ioctl(data->spifd, SPI_IOC_WR_MODE, &mode) < 0) {
        fprintf(stderr, "Failed to set SPI write mode: %s\n", strerror(errno));
        goto err;
    }
	if (ioctl(data->spifd, SPI_IOC_WR_BITS_PER_WORD, &wordsz) < 0) {
        fprintf(stderr, "Failed to set SPI word size: %s\n", strerror(errno));
        goto err;
    }
    if (ioctl(data->spifd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
        fprintf(stderr, "Failed to set SPI clock speed: %s\n", strerror(errno));
        goto err;
    }
    data->start_delay = LUX1310_MAGIC_ABN_DELAY;
    data->reg = fpga->sensor;

    /* Setup the sensor limits */
    data->sensor.fpga = fpga;
    data->sensor.ops = &lux1310_ops;
    data->sensor.name = "LUX1310";
    data->sensor.mfr = "Luxima";
    data->sensor.h_max_res = 1296;
    data->sensor.v_max_res = 1024;
    data->sensor.h_min_res = 336;
    data->sensor.v_min_res = 96;
    data->sensor.h_increment = LUX1310_HRES_INCREMENT;
    data->sensor.v_increment = 2;
    data->sensor.exp_min_nsec = LUX1310_MIN_EXPOSURE;
    data->sensor.exp_max_nsec = UINT32_MAX;
    data->sensor.pixel_rate = data->sensor.h_max_res * data->sensor.v_max_res * 1057;

    /* Determine the sensor type and set the appropriate pixel format. */
    color = open(ioport_find_by_name(iops, "lux1310-color"), O_RDONLY);
    if (color < 0) {
        data->sensor.format = FOURCC_CODE('Y', '1', '2', ' ');
    }
    else if (gpio_read(color)) {
        data->sensor.format = FOURCC_CODE('B', 'G', '1', '2');
        close(color);
    }
    else {
        data->sensor.format = FOURCC_CODE('Y', '1', '2', ' ');
        close(color);
    }

    /* Disable integration */
    fpga->sensor->frame_period = 100 * 4000;
    fpga->sensor->int_time = 100 * 4100;

    /* Setup the DAC to automatically update values on write. */
    gpio_write(data->daccs, 0);
    err = write(data->spifd, &dacmode, sizeof(dacmode));
    gpio_write(data->daccs, 1);
    if (err < 0) {
        fprintf(stderr, "Failed to set DAC update mode: %s\n", strerror(errno));
        goto err;
    }
    /* Write the default voltage settings and reset the LUX1310 */
    lux1310_set_voltage(data, LUX1310_DAC_VABL_VOLTAGE, 0.3);
    lux1310_set_voltage(data, LUX1310_DAC_VRSTB_VOLTAGE, 2.7);
    lux1310_set_voltage(data, LUX1310_DAC_VRST_VOLTAGE, 3.3);
    lux1310_set_voltage(data, LUX1310_DAC_VRSTL_VOLTAGE, 0.7);
    lux1310_set_voltage(data, LUX1310_DAC_VRSTH_VOLTAGE, 3.6);
    lux1310_set_voltage(data, LUX1310_DAC_VDR1_VOLTAGE, 2.5);
    lux1310_set_voltage(data, LUX1310_DAC_VDR2_VOLTAGE, 2.0);
    lux1310_set_voltage(data, LUX1310_DAC_VDR3_VOLTAGE, 1.5);

    /* Wait for the voltage levels to settle and strobe the reset low. */
    usleep(10000);
    fpga->sensor->control |= IMAGE_SENSOR_RESET_MASK;
    usleep(100);
    fpga->sensor->control &= ~IMAGE_SENSOR_RESET_MASK;
    usleep(1000);

    /* Reset the SCI registers to default. */
    lux1310_sci_write(fpga, LUX1310_SCI_SRESET_B >> LUX1310_SCI_REG_ADDR, 0);

    /* Perform automatic phase calibration. */
    err = lux1310_auto_phase_cal(data);
    if (err != 0) {
        goto err;
    }

    /* Set internal controls for performance fine-tuning */
    lux1310_write(data, LUX1310_SCI_LINE_VALID_DLY, 7);
    lux1310_write(data, LUX1310_SCI_STATE_IDLE_CTRL0, 0xe08e);
    lux1310_write(data, LUX1310_SCI_STATE_IDLE_CTRL1, 0xfc1f);
    lux1310_write(data, LUX1310_SCI_STATE_IDLE_CTRL2, 0x0003);
    lux1310_write(data, LUX1310_SCI_ADC_CLOCK_CTRL, 0x2202);
    lux1310_write_many(data,
            LUX1310_SCI_SEL_VCMI, 6,
            LUX1310_SCI_SEL_VCMO, 7,
            LUX1310_SCI_SEL_VCMP, 11,
            LUX1310_SCI_SEL_VCMN, 4,
            0);
    lux1310_write(data, LUX1310_SCI_INT_CLK_TIMING, 0x41f);

    /* Grab the sensor revision for further tuning. */
    rev = lux1310_read(data, LUX1310_SCI_REV_CHIP);
    fprintf(stderr, "configuring for LUX1310 silicon rev %d\n", rev);
    switch (rev) {
        case 2:
            lux1310_sci_write(fpga, 0x5B, 0x307f);
            lux1310_sci_write(fpga, 0x7B, 0x3007);
            break;

        case 1:
        default:
            lux1310_sci_write(fpga, 0x5B, 0x301f);
            lux1310_sci_write(fpga, 0x7B, 0x3001);
            break;
    } /* switch */

    /* Clear the ADC Offsets table, and then enable the ADC offset calibration. */
    for (i = 0; i < 16; i++) {
        lux1310_write(data, LUX1310_SCI_ADC_OS(i), 0);
    }
    lux1310_write(data, LUX1310_SCI_ADC_CAL_EN, 1);

    /* Program the default gain and wavetable.*/
    lux1310_write(data, LUX1310_SCI_GAIN_SEL_SAMP, 0x7f);
    lux1310_write(data, LUX1310_SCI_GAIN_SEL_FB, 0x7f);
    lux1310_write(data, LUX1310_SCI_GAIN_BIT, 3);
    lux1310_write_wavetab(data, &lux1310_wt_sram80);
    
    /* Enable the sensor timing engine. */
    lux1310_write(data, LUX1310_SCI_TIMING_EN, 1);
    usleep(10000);
    fpga->sensor->frame_period = 100 * 4000;
    fpga->sensor->int_time = 100 * 3900;
    usleep(50000);
    return &data->sensor;

err:
    close(data->daccs);
    close(data->spifd);
    free(data);
    return NULL;
} /* lux1310_init */
