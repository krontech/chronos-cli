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
#ifndef _FPGA_H
#define _FPGA_H

#include <stdint.h>
#include <stdio.h>

#define	GPMC_BASE                   0x50000000
#define	GPMC_RANGE_BASE             0x1000000

#define GPMC_REGISTER_OFFSET        0x0
#define	GPMC_REGISTER_LEN           0x1000000
#define GPMC_RAM_OFFSET             0x1000000
#define GPMC_RAM_LEN                0x1000000

/* TODO: Make this private to the LUX1310 driver. */
struct fpga_sensor {
    uint32_t control;
    uint32_t clk_phase;
    uint32_t sync_token;
    uint32_t data_correct;
    uint32_t fifo_start;
    uint32_t fifo_stop;
    uint32_t __reserved1[1];
    uint32_t frame_period;
    uint32_t int_time;
    uint32_t sci_control;
    uint32_t sci_address;
    uint32_t sci_datalen;
    uint32_t sci_fifo_addr;
    uint32_t sci_fifo_data;
};

struct fpga_seq {
    uint32_t control;
    uint32_t status;
    uint32_t frame_size;
    uint32_t region_start;
    uint32_t region_stop;
    uint32_t live_addr[3];
    uint32_t trig_delay;
    uint32_t md_fifo_read;
};

struct fpga_display {
    uint32_t control;
    uint32_t frame_address;
    uint32_t fpn_address;
    uint32_t gain;
    uint32_t h_period;
    uint32_t v_period;
    uint32_t h_sync_len;
    uint32_t v_sync_len;
    uint32_t h_back_porch;
    uint32_t v_back_porch;
    uint32_t h_res;
    uint32_t v_res;
    uint32_t h_out_res;
    uint32_t v_out_res;
    uint32_t peaking_thresh;
    uint32_t pipeline;
    uint32_t __reserved[1];
    uint32_t manual_sync;
};

//Register definitions from control register verilog file (in 16 bit word addresses)
#define SENSOR_CONTROL				0
#define SENSOR_CLK_PHASE			2
#define SENSOR_SYNC_TOKEN			4
#define SENSOR_DATA_CORRECT			6
#define SENSOR_FIFO_START_W_THRESH	8
#define SENSOR_FIFO_STOP_W_THRESH	10
#define	IMAGER_FRAME_PERIOD			14
#define IMAGER_INT_TIME				16
#define	SENSOR_SCI_CONTROL			18
#define	SENSOR_SCI_ADDRESS			20
#define	SENSOR_SCI_DATALEN			22
#define SENSOR_SCI_FIFO_WR_ADDR		24
#define SENSOR_SCI_READ_DATA		26

#define	SEQ_CONTROL 				32
#define	SEQ_STATUS					34
#define	SEQ_FRAME_SIZE				36
#define	SEQ_REC_REGION_START		38
#define	SEQ_REC_REGION_END			40
#define	SEQ_LIVE_ADDR_0				42
#define	SEQ_LIVE_ADDR_1				44
#define	SEQ_LIVE_ADDR_2				46
#define	SEQ_TRIG_DELAY				48
#define	SEQ_MD_FIFO_READ			50

#define SENSOR_MAGIC_START_DELAY	52
#define	SENSOR_LINE_PERIOD			54

#define	TRIG_ENABLE					0x50
#define	TRIG_INVERT					0x52
#define	TRIG_DEBOUNCE				0x54

#define	IO_OUT_LEVEL				0x56
#define	IO_OUT_SOURCE				0x58
#define	IO_OUT_INVERT				0x5A
#define	IO_IN						0x5C
#define	EXT_SHUTTER_CTL				0x5E

#define	SEQ_PGM_MEM_START			0x80
#define GPMC_PAGE_OFFSET			0x100

#define DISPLAY_CTL					0x200
#define	DISPLAY_FRAME_ADDRESS		0x202
#define	DISPLAY_FPN_ADDRESS			0x204
#define DISPLAY_GAIN				0x206
#define DISPLAY_H_PERIOD			0x208
#define DISPLAY_V_PERIOD			0x20A
#define DISPLAY_H_SYNC_LEN			0x20C
#define DISPLAY_V_SYNC_LEN			0x20E
#define DISPLAY_H_BACK_PORCH		0x210
#define DISPLAY_V_BACK_PORCH		0x212
#define DISPLAY_H_RES				0x214
#define DISPLAY_V_RES				0x216
#define DISPLAY_H_OUT_RES			0x218
#define DISPLAY_V_OUT_RES			0x220
#define DISPLAY_PEAKING_THRESH		0x222
#define DISPLAY_PIPELINE            0x224
#define DISPLAY_MANUAL_SYNC         0x228

#define CCM_ADDR                    0x260
#define CCM_11						0x260
#define CCM_12						0x262
#define CCM_13						0x264
#define CCM_21						0x268
#define CCM_22						0x26A
#define CCM_23						0x26C
#define CCM_31						0x26E
#define CCM_32						0x270
#define CCM_33						0x272
#define WL_DYNDLY_0					0x274
#define WL_DYNDLY_1					0x276
#define WL_DYNDLY_2					0x278
#define WL_DYNDLY_3					0x27A

#define MMU_CONFIG                  0x290

#define	SYSTEM_RESET				0x300
#define FPGA_VERSION				0x302
#define FPGA_SUBVERSION             0x304
#define	DCG_MEM_START				0x800

//Image sensor control register
#define	IMAGE_SENSOR_CONTROL_ADDR		0

#define	IMAGE_SENSOR_RESET_MASK			0x1
#define IMAGE_SENSOR_EVEN_TIMESLOT_MASK 0x2

//Phase control register
#define IMAGE_SENSOR_CLK_PHASE_OFFSET	0


//Data Correct
//Indicates data channels 11:0 and sync are correct
// Format: data[11], data[10], ... data[0], sync


//Image data write path FIFO thresholds for start and stop of data write to ram


#define SENSOR_SCI_CONTROL_RUN_MASK		0x1						//Write 1 to start, reads 1 while busy, 0 when done
#define SENSOR_SCI_CONTROL_RW_MASK		0x2						//Read == 1, Write == 0
#define SENSOR_SCI_CONTROL_FIFO_FULL_MASK	0x4					//1 indicates FIFO is full

#define	SEQ_CTL_SW_TRIG_MASK			0x1
#define SEQ_CTL_START_REC_MASK			0x2
#define	SEQ_CTL_STOP_REC_MASK			0x4
#define	SEQ_CTL_TRIG_DELAY_MODE_MASK	0x8

#define	SEQ_STATUS_RECORDING_MASK		0x1
#define	SEQ_STATUS_MD_FIFO_EMPTY_MASK	0x2

#define	EXT_SH_TRIGD_EXP_EN_MASK		0x1
#define	EXT_SH_TRIGD_EXP_EN_OFFSET		0
#define	EXT_SH_GATING_EN_MASK			0x2
#define	EXT_SH_GATING_EN_OFFSET			1
#define	EXT_SH_SRC_EN_MASK				0x1C
#define	EXT_SH_SRC_EN_OFFSET			2

#define DISPLAY_CTL_ADDRESS_SEL_OFFSET	0
#define DISPLAY_CTL_SCALER_NN_OFFSET	1
#define DISPLAY_CTL_SYNC_INH_OFFSET		2
#define DISPLAY_CTL_READOUT_INH_OFFSET	3
#define DISPLAY_CTL_COLOR_MODE_OFFSET	4
#define DISPLAY_CTL_FOCUS_PEAK_EN_OFFSET	5
#define DISPLAY_CTL_FOCUS_PEAK_COLOR_OFFSET	6
#define DISPLAY_CTL_ZEBRA_EN_OFFSET		9

#define DISPLAY_CTL_ADDRESS_SEL_MASK	(1 << DISPLAY_CTL_ADDRESS_SEL_OFFSET)
#define DISPLAY_CTL_SCALER_NN_MASK		(1 << DISPLAY_CTL_SCALER_NN_OFFSET)
#define DISPLAY_CTL_SYNC_INH_MASK		(1 << DISPLAY_CTL_SYNC_INH_OFFSET)
#define DISPLAY_CTL_READOUT_INH_MASK	(1 << DISPLAY_CTL_READOUT_INH_OFFSET)
#define DISPLAY_CTL_COLOR_MODE_MASK		(1 << DISPLAY_CTL_COLOR_MODE_OFFSET)
#define DISPLAY_CTL_FOCUS_PEAK_EN_MASK	(1 << DISPLAY_CTL_FOCUS_PEAK_EN_OFFSET)
#define DISPLAY_CTL_FOCUS_PEAK_COLOR_MASK	(7 << DISPLAY_CTL_FOCUS_PEAK_COLOR_OFFSET)
#define DISPLAY_CTL_ZEBRA_EN_MASK		(1 << DISPLAY_CTL_ZEBRA_EN_OFFSET)

#define SENSOR_DATA_WIDTH		        12
#define COLOR_MATRIX_INT_BITS	        3
#define COLOR_MATRIX_MAXVAL	            ((1 << SENSOR_DATA_WIDTH) * (1 << COLOR_MATRIX_INT_BITS))

#define MMU_INVERT_CS                   (1 << 0)
#define MMU_SWITCH_STUFFED              (1 << 1)

/* Runtime GPIO pins */
#define GPIO_DAC_CS             "/sys/class/gpio/gpio33/value"
#define GPIO_COLOR_SEL          "/sys/class/gpio/gpio34/value"
#define GPIO_TRIG_IO            "/sys/class/gpio/gpio127/value"
#define GPIO_ENCODER_A          "/sys/class/gpio/gpio20/value"
#define GPIO_ENCODER_B          "/sys/class/gpio/gpio26/value"
#define GPIO_ENCODER_SWITCH     "/sys/class/gpio/gpio27/value"
#define GPIO_SHUTTER_SWITCH     "/sys/class/gpio/gpio66/value"
#define GPIO_RECORD_LED_FRONT   "/sys/class/gpio/gpio41/value"
#define GPIO_RECORD_LED_BACK    "/sys/class/gpio/gpio25/value"
#define GPIO_FRAME_IRQ          "/sys/class/gpio/gpio51/value"

/* FPGA Programming pins */
#define GPIO_FPGA_PROGN         "/sys/class/gpio/gpio47/value"
#define GPIO_FPGA_INIT          "/sys/class/gpio/gpio45/value"
#define GPIO_FPGA_DONE          "/sys/class/gpio/gpio52/value"
#define GPIO_FPGA_CSEL          "/sys/class/gpio/gpio58/value"
#define GPIO_FPGA_HOLDN         "/sys/class/gpio/gpio53/value"

#define EEPROM_I2C_BUS          "/dev/i2c-1"

struct fpga {
    /* Memory mapping. */
    int fd;
    volatile uint16_t *reg;
    volatile uint16_t *ram;

    /* GPIOs and other peripherals */
    struct {
        int dac_cs;
        int color_sel;
        int trig_io;
        int enc_a;
        int enc_b;
        int enc_sw;
        int shutter;
        int led_front;
        int led_back;
        int frame_irq;
    } gpio;

    /* Structured access to FPGA registers. */
    volatile struct fpga_sensor *sensor;
    volatile struct fpga_seq    *seq;
    volatile struct fpga_display *display;
    volatile uint32_t           *cc_matrix;
};

struct fpga *fpga_open(void);
void fpga_close(struct fpga *fpga);
int fpga_load(const char *spi, const char *bitstream, FILE *log);

#endif /* _FPGA_H */
