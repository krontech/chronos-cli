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

#define LUX1310_CLOCKS_TO_NSEC(_clks_, _hz_) \
    ((((_clks_) * 1000000000ULL) + (_hz_) - 1) / (_hz_))

#define LUX1310_SENSOR_CLOCK_RATE   90000000ULL
#define LUX1310_TIMING_CLOCK_RATE   100000000ULL
#define LUX1310_MIN_EXPOSURE        1000
#define LUX1310_MIN_WAVETABLE_SIZE  20
#define LUX1310_MAGIC_ABN_DELAY     26
#define LUX1310_HRES_INCREMENT      LUX1310_ADC_COUNT

#define LUX1310_BLACK_COLS          32
#define LUX1310_BLACK_ROWS          8

#define LUX1310_ADC_COUNT           16
#define LUX1310_ADC_OFFSET_BITS     10

#define LUX1310_DAC_FULL_SCALE      4095
#define LUX1310_DAC_VREF            3300 /* 3300mV */
#define LUX1310_DAC_AUTOUPDATE      9

/* Which DAC output controls what sensor voltages */
#define LUX1310_DAC_VDR3        0
#define LUX1310_DAC_VABL        1
#define LUX1310_DAC_VDR1        2
#define LUX1310_DAC_VDR2        3
#define LUX1310_DAC_VRSTB       4
#define LUX1310_DAC_VRSTH       5
#define LUX1310_DAC_VRSTL       6
#define LUX1310_DAC_VRST        7

/* Gain networks on some DAC outputs. */
#define LUX1310_VABL_MUL        (100 + 232)
#define LUX1310_VABL_DIV        (100)
#define LUX1310_VRSTH_MUL       (499)
#define LUX1310_VRSTH_DIV       (499 + 100)
#define LUX1310_VRSTL_MUL       (100 + 232)
#define LUX1310_VRSTL_DIV       (100)
#define LUX1310_VRST_MUL        (499)
#define LUX1310_VRST_DIV        (499 + 100)

struct lux1310_private_data {
    struct image_sensor sensor;
    volatile struct fpga_sensor *reg;
    const struct lux1310_gaintab *gaintab;
    const struct lux1310_wavetab *wavetab;
    int spifd;
    int daccs;
};

/* Gain Configuration Table */
static const struct lux1310_gaintab lux1310_gain_data[] = {
    {   /* x1 - 0dB */
        .vrstb = 2700,
        .vrst = 3300,
        .vrsth = 3600,
        .sampling = 0x7f,
        .feedback = 0x7f,
        .gain_bit = 3,
        .analog_gain = 0,
    },
    { /* x2 - 6dB */
        .vrstb = 2700,
        .vrst = 3300,
        .vrsth = 3600,
        .sampling = 0xfff,
        .feedback = 0x7f,
        .gain_bit = 3,
        .analog_gain = 6,
    },
    { /* x4 - 12dB */
        .vrstb = 2700,
        .vrst = 3300,
        .vrsth = 3600,
        .sampling = 0xfff,
        .feedback = 0x7f,
        .gain_bit = 0,
        .analog_gain = 12,
    },
    { /* x8 - 18dB */
        .vrstb = 1700,
        .vrst = 2300,
        .vrsth = 2600,
        .sampling = 0xfff,
        .feedback = 0x7,
        .gain_bit = 0,
        .analog_gain = 18,
    },
    { /* x16 - 24dB */
        .vrstb = 1700,
        .vrst = 2300,
        .vrsth = 2600,
        .sampling = 0xfff,
        .feedback = 0x1,
        .gain_bit = 0,
        .analog_gain = 24,
    }
};

static uint16_t
lux1310_sci_read(struct lux1310_private_data *data, uint8_t addr)
{
    uint16_t first;
    int i;

    /* Set RW, address and length. */
    data->reg->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    data->reg->sci_address = addr;
    data->reg->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    data->reg->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = data->reg->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (data->reg->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

    if (!first && (i != 0)) {
        fprintf(stderr, "lux1310_sci_read: Read first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "lux1310_sci_read: Read busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }

    usleep(1000);
    return data->reg->sci_fifo_read;
} /* lux1310_sci_read */

static void
lux1310_sci_write(struct lux1310_private_data *data, uint8_t addr, uint16_t value)
{
    uint16_t first;
    int i;

    /* Clear RW, and setup the transfer and fill the FIFO */
    data->reg->sci_control &= ~SENSOR_SCI_CONTROL_RW_MASK;
    data->reg->sci_address = addr;
    data->reg->sci_datalen = 2;
    data->reg->sci_fifo_write = (value >> 8) & 0xff;
    data->reg->sci_fifo_write = (value >> 0) & 0xff;

    /* Start the transfer and then wait for completion. */
    data->reg->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = data->reg->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (data->reg->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
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
lux1310_sci_writebuf(struct lux1310_private_data *data, uint8_t addr, const void *buf, size_t len)
{
    size_t i;
    const uint8_t *p = buf;

    /* Clear RW, and setup the transfer and fill the FIFO */
    data->reg->sci_control &= ~SENSOR_SCI_CONTROL_RW_MASK;
    data->reg->sci_address = addr;
    data->reg->sci_datalen = len;
    for (i = 0; i < len; i++) {
        data->reg->sci_fifo_write = *p++;
    }

    /* Start the transfer and then wait for completion. */
    data->reg->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    while((data->reg->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) != 0) { /*nop */ }
} /* lux1310_sci_writebuf */

/* Write a voltage level to the DAC */
static int
lux1310_set_voltage(struct lux1310_private_data *data, unsigned int channel, unsigned int mv, unsigned int mul, unsigned int div)
{
    unsigned int vdac = ((unsigned long long)LUX1310_DAC_FULL_SCALE * mv * mul) / (LUX1310_DAC_VREF * div);
    uint16_t reg = htole16((((channel & 0x7) << 12) | vdac));
    int err; 

    gpio_write(data->daccs, 0);
    err = write(data->spifd, &reg, sizeof(reg));
    gpio_write(data->daccs, 1);
    return err;
} /* lux1310_set_voltage */

static unsigned int
lux1310_read(struct lux1310_private_data *data, unsigned int reg)
{
    unsigned int val = lux1310_sci_read(data, reg >> LUX1310_SCI_REG_ADDR);
    return getbits(val, reg & LUX1310_SCI_REG_MASK);
} /* lux1310_read */

/* Perform a simple register write, which contains only one sub-field. */
static void
lux1310_write(struct lux1310_private_data *data, unsigned int reg, unsigned int val)
{
    lux1310_sci_write(data, reg >> LUX1310_SCI_REG_ADDR, setbits(val, reg & LUX1310_SCI_REG_MASK));
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
    lux1310_sci_write(data, addr, val);
} /* lux1310_write_many */

/* Program a wave table into the image sensor and configure the readout delays to match. */
static void
lux1310_write_wavetab(struct lux1310_private_data *data, const struct lux1310_wavetab *wave)
{
    /* Store the current wavetable for later. */
    data->wavetab = wave;

    lux1310_write(data, LUX1310_SCI_TIMING_EN, 0);
    lux1310_write(data, LUX1310_SCI_RDOUT_DLY, wave->read_delay);
    lux1310_write(data, LUX1310_SCI_WAVETAB_SIZE, wave->read_delay);
    lux1310_sci_writebuf(data, 0x7F, wave->table, wave->len);
    lux1310_write(data, LUX1310_SCI_TIMING_EN, 1);
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

/* Return the minimum period (in clocks) for a given resolution and wave table length. */
static unsigned long long
lux1310_min_period(const struct image_geometry *g, unsigned long int wavelen)
{
    const unsigned long t_hblank = 2;
    const unsigned long t_tx = 25;
    const unsigned long t_fovf = 50;
    const unsigned long t_fovb = 50; /* Duration between PRSTN falling and TXN falling (I think) */
    
    /* Sum up the minimum number of clocks to read a frame at this resolution */
    unsigned long t_read = (g->hres / LUX1310_HRES_INCREMENT);
    unsigned long t_row = max(t_read + t_hblank, wavelen + 3);
    return (t_row * g->vres) + t_tx + t_fovf + t_fovb;
} /* lux1310_min_period */

/* Configure the sensor to a given resolution, or return -1 on error. */
static int
lux1310_set_resolution(struct image_sensor *s, const struct image_geometry *g)
{
    struct lux1310_private_data *data = CONTAINER_OF(s, struct lux1310_private_data, sensor);

	uint32_t h_start = g->hoffset / LUX1310_HRES_INCREMENT;
	uint32_t h_width = g->hres / LUX1310_HRES_INCREMENT;
    lux1310_write(data, LUX1310_SCI_X_START, LUX1310_BLACK_COLS + h_start * LUX1310_HRES_INCREMENT);
	lux1310_write(data, LUX1310_SCI_X_END, LUX1310_BLACK_COLS + (h_start + h_width) * LUX1310_HRES_INCREMENT - 1);
	lux1310_write(data, LUX1310_SCI_Y_START, g->voffset);
	lux1310_write(data, LUX1310_SCI_Y_END, g->voffset + g->vres - 1);
    return 0;
} /* lux1310_set_resolution */

/* Return the timing constraings for a given frame geometry. */
static int
lux1310_constraints(struct image_sensor *sensor, const struct image_geometry *g, struct image_constraints *c)
{
    struct lux1310_private_data *data = CONTAINER_OF(sensor, struct lux1310_private_data, sensor);
    unsigned long t_line = max((g->hres / LUX1310_HRES_INCREMENT)+2, (LUX1310_MIN_WAVETABLE_SIZE + 3));

    c->t_max_period = UINT32_MAX;
    c->t_min_period = LUX1310_CLOCKS_TO_NSEC(lux1310_min_period(g, LUX1310_MIN_WAVETABLE_SIZE), LUX1310_TIMING_CLOCK_RATE);
    c->f_quantization = LUX1310_TIMING_CLOCK_RATE;

    c->t_min_exposure = LUX1310_CLOCKS_TO_NSEC(t_line, LUX1310_TIMING_CLOCK_RATE);
    c->t_max_shutter = 360;
    c->t_exposure_delay = LUX1310_CLOCKS_TO_NSEC(LUX1310_MAGIC_ABN_DELAY, LUX1310_TIMING_CLOCK_RATE);

    return 0;
} /* lux1310_constraints */

/* Round the exposure timing to the nearest value the sensor can accept. */
static unsigned long long
lux1310_round_exposure(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec)
{
    /* TODO: Must ensure that the exposure time < (period - overhead). */
    if (nsec < LUX1310_MIN_EXPOSURE) {
        return LUX1310_MIN_EXPOSURE;
    }
    /* Round up to the nearest FPGA timing clock tick. */
    nsec += ((1000000000 / LUX1310_TIMING_CLOCK_RATE) / 2);
    return nsec - (nsec % (1000000000 / LUX1310_TIMING_CLOCK_RATE));
} /* lux1310_round_exposure */

static unsigned long long
lux1310_round_period(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec)
{
    unsigned long t_period = (nsec * 1000000000) / LUX1310_SENSOR_CLOCK_RATE;
    unsigned long t_frame = lux1310_min_period(g, LUX1310_MIN_WAVETABLE_SIZE);
    return (max(t_frame, t_period) * LUX1310_SENSOR_CLOCK_RATE) / 1000000000;
} /* lux1310_round_period */

static int
lux1310_set_period(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec)
{
    struct lux1310_private_data *data = CONTAINER_OF(sensor, struct lux1310_private_data, sensor);
    unsigned long t_frame = ((nsec * LUX1310_TIMING_CLOCK_RATE) + 999999999) / 1000000000;
    data->reg->frame_period = t_frame;

    /* Program the longest wavetable for this frame period. */
    /* TODO: Replace this with a loop over a database of known tables. */
    /* TODO: Automagic wavetable generation would go here. */
    if (t_frame >= lux1310_min_period(g, 80)) {
        lux1310_write_wavetab(data, &lux1310_wt_sram80);
    }
    else if (t_frame >= lux1310_min_period(g, 39)) {
        lux1310_write_wavetab(data, &lux1310_wt_sram39);
    }
    else if (t_frame >= lux1310_min_period(g, 30)) {
        lux1310_write_wavetab(data, &lux1310_wt_sram30);
    }
    else if (t_frame >= lux1310_min_period(g, 25)) {
        lux1310_write_wavetab(data, &lux1310_wt_sram25);
    }
    else {
        lux1310_write_wavetab(data, &lux1310_wt_sram20);
    }

    /* Setup the timing generator to handle the the line period and start delay. */
    /* TODO: Would it be cleaner to move these registers into the block of sensor stuff? */
    sensor->fpga->reg[SENSOR_MAGIC_START_DELAY] = data->wavetab->start_delay;
    sensor->fpga->reg[SENSOR_LINE_PERIOD] = max((g->hres / LUX1310_HRES_INCREMENT)+2, (data->wavetab->read_delay + 3)) - 1;
    return 0;
} /* lux1310_set_period */

static int
lux1310_set_exposure(struct image_sensor *sensor, const struct image_geometry *g, unsigned long long nsec)
{
    struct lux1310_private_data *data = CONTAINER_OF(sensor, struct lux1310_private_data, sensor);
    /* Compute timing first in units of sensor clock periods. */
    unsigned long t_line = max((g->hres / LUX1310_HRES_INCREMENT)+2, (data->wavetab->read_delay + 3));
    unsigned long t_exposure = (nsec * LUX1310_SENSOR_CLOCK_RATE + 500000000) / 1000000000;
    unsigned long t_start = LUX1310_MAGIC_ABN_DELAY;

    /*
     * Set the exposure time in units of FPGA timing clock periods, while keeping the
     * exposure as a multiple of the horizontal readout time (to fix the horizontal
     * line issue).
     */
    uint32_t exp_lines = (t_exposure + t_line/2) / t_line;
    data->reg->int_time = (t_start + (t_line * exp_lines)) * LUX1310_TIMING_CLOCK_RATE / LUX1310_SENSOR_CLOCK_RATE;
} /* lux1310_set_exposure */

static int
lux1310_set_gain(struct image_sensor *sensor, int gain, FILE *calfile)
{
    struct lux1310_private_data *data = CONTAINER_OF(sensor, struct lux1310_private_data, sensor);
    int16_t caldata[LUX1310_ADC_COUNT];
    int i, j;
    for (i = 0; i < ARRAY_SIZE(lux1310_gain_data); i++) {
        if (lux1310_gain_data[i].analog_gain != gain) continue;
        /* Program the voltage references. */
        lux1310_set_voltage(data, LUX1310_DAC_VRSTB, lux1310_gain_data[i].vrstb, 1, 1);
        lux1310_set_voltage(data, LUX1310_DAC_VRST, lux1310_gain_data[i].vrst, LUX1310_VRST_MUL, LUX1310_VRST_DIV);
        lux1310_set_voltage(data, LUX1310_DAC_VRSTH, lux1310_gain_data[i].vrsth, LUX1310_VRSTH_MUL, LUX1310_VRST_DIV);
        
        /* Program the gain calibration. */
        lux1310_write(data, LUX1310_SCI_GAIN_SEL_SAMP, lux1310_gain_data[i].sampling);
        lux1310_write(data, LUX1310_SCI_GAIN_SEL_FB, lux1310_gain_data[i].feedback);
        lux1310_write(data, LUX1310_SCI_GAIN_BIT, lux1310_gain_data[i].gain_bit);

        /* Adjust the ADC offsets if calibration data is provided. */
        if (calfile) {
            fread(caldata, sizeof(caldata[0]), LUX1310_ADC_COUNT, calfile);
        } else {
            memset(caldata, 0, sizeof(caldata));
        }
        for (j = 0; j < sensor->adc_count; j++) {
            uint16_t x = abs(caldata[j]) & ((1 << LUX1310_ADC_OFFSET_BITS) - 1);
            if (caldata[j]) x |= (1 << LUX1310_ADC_OFFSET_BITS);
            lux1310_write(data, LUX1310_SCI_ADC_OS(j), x);
        }
        data->gaintab = &lux1310_gain_data[i];
        return 0;
    }

    /* Not a valid gain for the LUX1310. */
    return -1;
} /* lux1310_set_gain */

/* Read a 12-bit pixel out of a packed frame buffer. */
static uint16_t
lux1310_get_pixel(const struct image_geometry *g, const void *frame, unsigned long x, unsigned long y)
{
    unsigned long offset = ((y * g->hres) + x) * 12 / 8;
    const uint8_t *pxdata = frame;
    uint16_t pixel = (pxdata[offset] << 8) | (pxdata[offset+1] << 0);
    if (x & 1) {
        return (pixel >> 4) & 0xfff;
    } else {
        return (pixel >> 0) & 0xfff;
    }
} /* lux1310_get_pixel */

/* Compute gain calibration data when given a raw frame. */
static int
lux1310_cal_gain(struct image_sensor *sensor, const struct image_geometry *g, const void *frame, FILE *fp)
{
    struct lux1310_private_data *data = CONTAINER_OF(sensor, struct lux1310_private_data, sensor);
    int16_t caldata[LUX1310_ADC_COUNT];
    uint32_t minvals[LUX1310_ADC_COUNT];
    int i;

    for (i = 0; i < LUX1310_ADC_COUNT; i++) {
        minvals[i] = UINT32_MAX;
    }
    /* TODO: Examine multiple rows? */
    for (i = 0; i < g->hres; i++) {
        uint32_t pixel = lux1310_get_pixel(g, frame, i, 0);
        minvals[i % LUX1310_ADC_COUNT] = min(pixel, minvals[i % LUX1310_ADC_COUNT]);
    }

    for (i = 0; i < LUX1310_ADC_COUNT; i++) {
        caldata[i] = -(minvals[i] - 30)/2;
        if (caldata[i] > 1023) caldata[i] = 1023;
        if (caldata[i] < -1023) caldata[i] = -1023;
        lux1310_write(data, LUX1310_SCI_ADC_OS(i), caldata[i]);
    }
    return (fwrite(caldata, sizeof(caldata[0]), LUX1310_ADC_COUNT, fp) == LUX1310_ADC_COUNT) ? 0 : -1;
} /* lux1310_cal_gain */

/* Return a file suffix for the lux1310 calibration data - which depends on gain and wavetable. */
static char *
lux1310_cal_suffix(struct image_sensor *sensor, char *buf, size_t maxlen)
{
    struct lux1310_private_data *data = CONTAINER_OF(sensor, struct lux1310_private_data, sensor);
    unsigned int gain_mul = 1 << (data->gaintab->analog_gain / 6);
    snprintf(buf, maxlen, "_G%u_WT%d", gain_mul, data->wavetab->read_delay);
    return buf;
} /* lux1310_cal_suffix */

static const struct image_sensor_ops lux1310_ops = {
    .set_exposure = lux1310_set_exposure,
    .set_period = lux1310_set_period,
    .set_resolution = lux1310_set_resolution,
    .get_constraints = lux1310_constraints,
    .set_gain = lux1310_set_gain,
    .cal_gain = lux1310_cal_gain,
    .cal_suffix = lux1310_cal_suffix,
    /* TODO: Calibration Data and API */
};

struct image_sensor *
lux1310_init(struct fpga *fpga, const struct ioport *iops)
{
    struct lux1310_private_data *data;
    uint32_t hz = 1000000;
    uint8_t mode = 1;
    uint8_t wordsz = 16;
    uint16_t dacmode = htole16(LUX1310_DAC_AUTOUPDATE << 12);
    uint16_t rev;
    int color;
    int err;
    int i;

    data = malloc(sizeof(struct lux1310_private_data));
    if (!data) {
        return NULL;
    }
    data->spifd = ioport_open(iops, "lux1310-spidev", O_RDWR);
    if (data->spifd < 0) {
        fprintf(stderr, "Failed to open spidev device: %s\n", strerror(errno));
        free(data);
        return NULL;
    }
    data->daccs = ioport_open(iops, "lux1310-dac-cs", O_WRONLY);
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
    data->reg = fpga->sensor;
    data->reg->fifo_start = 0x100;
    data->reg->fifo_stop = 0x100;

    /* Setup the sensor limits */
    data->sensor.fpga = fpga;
    data->sensor.ops = &lux1310_ops;
    data->sensor.name = "lux1310";
    data->sensor.mfr = "Luxima";
    //data->sensor.h_max_res = 1296;
    data->sensor.h_max_res = 1280;
    data->sensor.v_max_res = 1024;
    data->sensor.h_min_res = 336;
    data->sensor.v_min_res = 96;
    data->sensor.h_increment = LUX1310_HRES_INCREMENT;
    data->sensor.v_increment = 2;
    data->sensor.pixel_rate = data->sensor.h_max_res * data->sensor.v_max_res * 1057;
    data->sensor.adc_count = LUX1310_ADC_COUNT;

    /* Determine the sensor type and set the appropriate pixel format. */
    color = ioport_open(iops, "lux1310-color", O_RDONLY);
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
    data->reg->frame_period = 100 * 4000;
    data->reg->int_time = 100 * 4100;

    /* Setup the DAC to automatically update values on write. */
    gpio_write(data->daccs, 0);
    err = write(data->spifd, &dacmode, sizeof(dacmode));
    gpio_write(data->daccs, 1);
    if (err < 0) {
        fprintf(stderr, "Failed to set DAC update mode: %s\n", strerror(errno));
        goto err;
    }
    /* Write the default voltage settings and reset the LUX1310 */
    lux1310_set_voltage(data, LUX1310_DAC_VABL, 300, LUX1310_VABL_MUL, LUX1310_VABL_DIV);
    lux1310_set_voltage(data, LUX1310_DAC_VRSTB, 2700, 1, 1);
    lux1310_set_voltage(data, LUX1310_DAC_VRST, 3300, LUX1310_VRST_MUL, LUX1310_VRST_DIV);
    lux1310_set_voltage(data, LUX1310_DAC_VRSTL, 700, LUX1310_VRSTL_MUL, LUX1310_VRSTL_DIV);
    lux1310_set_voltage(data, LUX1310_DAC_VRSTH, 3600, LUX1310_VRSTH_MUL, LUX1310_VRSTH_DIV);;
    lux1310_set_voltage(data, LUX1310_DAC_VDR1, 2500, 1, 1);
    lux1310_set_voltage(data, LUX1310_DAC_VDR2, 2000, 1, 1);
    lux1310_set_voltage(data, LUX1310_DAC_VDR3, 1500, 1, 1);

    /* Wait for the voltage levels to settle and strobe the reset low. */
    usleep(10000);
    data->reg->control |= IMAGE_SENSOR_RESET_MASK;
    usleep(100);
    data->reg->control &= ~IMAGE_SENSOR_RESET_MASK;
    usleep(1000);

    /* Reset the SCI registers to default. */
    lux1310_sci_write(data, LUX1310_SCI_SRESET_B >> LUX1310_SCI_REG_ADDR, 0);

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
            lux1310_sci_write(data, 0x5B, 0x307f);
            lux1310_sci_write(data, 0x7B, 0x3007);
            break;

        case 1:
        default:
            lux1310_sci_write(data, 0x5B, 0x301f);
            lux1310_sci_write(data, 0x7B, 0x3001);
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
    lux1310_set_gain(&data->sensor, 0, NULL);
    
    /* Enable the sensor timing engine. */
    lux1310_write(data, LUX1310_SCI_TIMING_EN, 1);
    usleep(10000);
    data->reg->frame_period = 100 * 4000;
    data->reg->int_time = 100 * 3900;
    usleep(50000);
    return &data->sensor;

err:
    close(data->daccs);
    close(data->spifd);
    free(data);
    return NULL;
} /* lux1310_init */
