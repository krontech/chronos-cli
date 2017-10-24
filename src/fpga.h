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

struct fpga_sensor {
    uint16_t control;
    uint16_t __reserved0[1];
    uint16_t clk_phase;
    uint16_t __reserved1[1];
    uint16_t sync_token;
    uint16_t __reserved2[1];
    uint16_t data_correct;
    uint16_t __reserved3[1];
    uint16_t fifo_start;
    uint16_t __reserved4[1];
    uint16_t fifo_stop;
    uint16_t __reserved5[1];
    uint16_t frame_period;
    uint16_t __reserved6[3];
    uint16_t int_time;
    uint16_t __reserved7[1];
    uint16_t sci_control;
    uint16_t __reserved8[1];
    uint16_t sci_address;
    uint16_t __reserved9[1];
    uint16_t sci_datalen;
    uint16_t __reserved10[1];
    uint16_t sci_fifo_addr;
    uint16_t __reserved11[1];
    uint16_t sci_fifo_data;
};

struct fpga_seq {
    uint16_t control;
    uint16_t __reserved0[1];
    uint16_t status;
    uint16_t __reserved1[1];
    uint16_t frame_size;
    uint16_t __reserved2[1];
    uint16_t region_start;
    uint16_t __reserved3[1];
    uint16_t region_stop;
    uint16_t __reserved4[1];
    uint32_t live_addr[3];
    uint16_t trig_delay;
    uint16_t __reserved5[1];
    uint16_t md_fifo_read;
};

struct fpga_display {
    uint16_t control;
    uint16_t __reserved0[1];
    uint16_t frame_address;
    uint16_t __reserved1[1];
    uint16_t fpn_address;
    uint16_t __reserved2[1];
    uint16_t gain;
    uint16_t __reserved3[1];
    uint16_t h_period;
    uint16_t __reserved4[1];
    uint16_t v_period;
    uint16_t __reserved5[1];
    uint16_t h_sync_len;
    uint16_t __reserved6[1];
    uint16_t v_sync_len;
    uint16_t __reserved7[1];
    uint16_t h_back_porch;
    uint16_t __reserved8[1];
    uint16_t v_back_porch;
    uint16_t __reserved9[1];
    uint16_t h_res;
    uint16_t __reserved11[1];
    uint16_t v_res;
    uint16_t __reserved12[1];
    uint16_t h_out_res;
    uint16_t __reserved13[1];
    uint16_t v_out_res;
    uint16_t __reserved14[1];
    uint16_t peaking_thresh;
    uint16_t __reserved15[1];
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

#define	SEQ_CTL						32
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

#define	SYSTEM_RESET				0x300
#define FPGA_VERSION				0x302
#define	DCG_MEM_START				0x800

//Image sensor control register
#define	IMAGE_SENSOR_CONTROL_ADDR		0

#define	IMAGE_SENSOR_RESET_MASK			0x1
#define IMAGE_SENSOR_EVEN_TIMESLOT_MASK 0x2

//Phase control register

#define IMAGE_SENSOR_CLK_PHASE_ADDR		(SENSOR_CLK_PHASE * 2)
#define IMAGE_SENSOR_CLK_PHASE_OFFSET	0

//Sync Token Register
#define IMAGE_SENSOR_SYNC_TOKEN_ADDR	(SENSOR_SYNC_TOKEN * 2)

//Data Correct
//Indicates data channels 11:0 and sync are correct
// Format: data[11], data[10], ... data[0], sync

#define IMAGE_SENSOR_DATA_CORRECT_ADDR	(SENSOR_DATA_CORRECT * 2)

//Image data write path FIFO thresholds for start and stop of data write to ram

#define IMAGE_SENSOR_FIFO_START_W_THRESH_ADDR	(SENSOR_FIFO_START_W_THRESH	* 2)
#define IMAGE_SENSOR_FIFO_STOP_W_THRESH_ADDR	(SENSOR_FIFO_STOP_W_THRESH * 2)

#define	IMAGER_FRAME_PERIOD_ADDR			(IMAGER_FRAME_PERIOD * 2)
#define IMAGER_INT_TIME_ADDR				(IMAGER_INT_TIME * 2)

#define	SENSOR_SCI_CONTROL_ADDR			(SENSOR_SCI_CONTROL * 2)
#define SENSOR_SCI_CONTROL_RUN_MASK		0x1						//Write 1 to start, reads 1 while busy, 0 when done
#define SENSOR_SCI_CONTROL_RW_MASK		0x2						//Read == 1, Write == 0
#define SENSOR_SCI_CONTROL_FIFO_FULL_MASK	0x4					//1 indicates FIFO is full
#define	SENSOR_SCI_ADDRESS_ADDR			(SENSOR_SCI_ADDRESS * 2)
#define	SENSOR_SCI_DATALEN_ADDR			(SENSOR_SCI_DATALEN * 2)
#define SENSOR_SCI_FIFO_WR_ADDR_ADDR	(SENSOR_SCI_FIFO_WR_ADDR * 2)
#define SENSOR_SCI_READ_DATA_ADDR		(SENSOR_SCI_READ_DATA * 2)

#define	SEQ_CTL_ADDR					(SEQ_CTL * 2)
#define	SEQ_CTL_SW_TRIG_MASK			0x1
#define SEQ_CTL_START_REC_MASK			0x2
#define	SEQ_CTL_STOP_REC_MASK			0x4
#define	SEQ_CTL_TRIG_DELAY_MODE_MASK	0x8

#define	SEQ_STATUS_ADDR					(SEQ_STATUS * 2)
#define	SEQ_STATUS_RECORDING_MASK		0x1
#define	SEQ_STATUS_MD_FIFO_EMPTY_MASK	0x2

#define	SEQ_FRAME_SIZE_ADDR				(SEQ_FRAME_SIZE * 2)
#define	SEQ_REC_REGION_START_ADDR		(SEQ_REC_REGION_START * 2)
#define	SEQ_REC_REGION_END_ADDR			(SEQ_REC_REGION_END * 2)
#define	SEQ_LIVE_ADDR_0_ADDR			(SEQ_LIVE_ADDR_0 * 2)
#define	SEQ_LIVE_ADDR_1_ADDR			(SEQ_LIVE_ADDR_1 * 2)
#define	SEQ_LIVE_ADDR_2_ADDR			(SEQ_LIVE_ADDR_2 * 2)
#define	SEQ_TRIG_DELAY_ADDR				(SEQ_TRIG_DELAY * 2)
#define	SEQ_MD_FIFO_READ_ADDR			(SEQ_MD_FIFO_READ * 2)
#define	SEQ_PGM_MEM_START_ADDR			(SEQ_PGM_MEM_START * 2)

#define SENSOR_MAGIC_START_DELAY_ADDR	(SENSOR_MAGIC_START_DELAY * 2)
#define	SENSOR_LINE_PERIOD_ADDR			(SENSOR_LINE_PERIOD * 2)

#define	TRIG_ENABLE_ADDR				(TRIG_ENABLE * 2)
#define	TRIG_INVERT_ADDR				(TRIG_INVERT * 2)
#define	TRIG_DEBOUNCE_ADDR				(TRIG_DEBOUNCE * 2)

#define	IO_OUT_LEVEL_ADDR				(IO_OUT_LEVEL * 2)		//1 outputs high if selected (invert does not affect this)
#define	IO_OUT_SOURCE_ADDR				(IO_OUT_SOURCE * 2)		//1 selects int time, 0 selects IO_OUT_LEVEL
#define	IO_OUT_INVERT_ADDR				(IO_OUT_INVERT * 2)		//1 inverts int time signal
#define	IO_IN_ADDR						(IO_IN * 2)		//1 inverts int time signal

#define	EXT_SHUTTER_CTL_ADDR			(EXT_SHUTTER_CTL * 2)
#define	EXT_SH_TRIGD_EXP_EN_MASK		0x1
#define	EXT_SH_TRIGD_EXP_EN_OFFSET		0
#define	EXT_SH_GATING_EN_MASK			0x2
#define	EXT_SH_GATING_EN_OFFSET			1
#define	EXT_SH_SRC_EN_MASK				0x1C
#define	EXT_SH_SRC_EN_OFFSET			2

#define GPMC_PAGE_OFFSET_ADDR			(GPMC_PAGE_OFFSET * 2)

#define DISPLAY_CTL_ADDR				(DISPLAY_CTL * 2)
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

#define	DISPLAY_FRAME_ADDRESS_ADDR		(DISPLAY_FRAME_ADDRESS * 2)
#define	DISPLAY_FPN_ADDRESS_ADDR		(DISPLAY_FPN_ADDRESS * 2)
#define DISPLAY_GAIN_ADDR				(DISPLAY_GAIN * 2)
#define DISPLAY_H_PERIOD_ADDR			(DISPLAY_H_PERIOD * 2)
#define DISPLAY_V_PERIOD_ADDR			(DISPLAY_V_PERIOD * 2)
#define DISPLAY_H_SYNC_LEN_ADDR			(DISPLAY_H_SYNC_LEN * 2)
#define DISPLAY_V_SYNC_LEN_ADDR			(DISPLAY_V_SYNC_LEN * 2)
#define DISPLAY_H_BACK_PORCH_ADDR		(DISPLAY_H_BACK_PORCH * 2)
#define DISPLAY_V_BACK_PORCH_ADDR		(DISPLAY_V_BACK_PORCH * 2)
#define DISPLAY_H_RES_ADDR				(DISPLAY_H_RES * 2)
#define DISPLAY_V_RES_ADDR				(DISPLAY_V_RES * 2)
#define DISPLAY_H_OUT_RES_ADDR			(DISPLAY_H_OUT_RES * 2)
#define DISPLAY_V_OUT_RES_ADDR			(DISPLAY_V_OUT_RES * 2)
#define DISPLAY_PEAKING_THRESH_ADDR		(DISPLAY_PEAKING_THRESH * 2)

#define CCM_11_ADDR						(CCM_11 * 2)
#define CCM_12_ADDR						(CCM_12 * 2)
#define CCM_13_ADDR						(CCM_13 * 2)
#define CCM_21_ADDR						(CCM_21 * 2)
#define CCM_22_ADDR						(CCM_22 * 2)
#define CCM_23_ADDR						(CCM_23 * 2)
#define CCM_31_ADDR						(CCM_31 * 2)
#define CCM_32_ADDR						(CCM_32 * 2)
#define CCM_33_ADDR						(CCM_33 * 2)

#define WL_DYNDLY_0_ADDR				(WL_DYNDLY_0 * 2)
#define WL_DYNDLY_1_ADDR				(WL_DYNDLY_1 * 2)
#define WL_DYNDLY_2_ADDR				(WL_DYNDLY_2 * 2)
#define WL_DYNDLY_3_ADDR				(WL_DYNDLY_3 * 2)

#define SYSTEM_RESET_ADDR				(SYSTEM_RESET * 2)

#define FPGA_VERSION_ADDR				(FPGA_VERSION * 2)

#define	DCG_MEM_START_ADDR				(DCG_MEM_START * 2)

struct fpga {
    /* Memory mapping. */
    int fd;
    volatile uint16_t *reg;
    volatile uint16_t *ram;

    /* Structured access to FPGA registers. */
    volatile struct fpga_sensor *sensor;
    volatile struct fpga_seq    *seq;
    volatile struct fpga_display *display;
};

struct fpga *fpga_open(void);
void fpga_close(struct fpga *fpga);
int fpga_load(const char *spi, const char *bitstream, FILE *log);

/* SCI Accesses */
uint16_t fpga_sci_read(struct fpga *fpga, uint8_t addr);
void fpga_sci_write(struct fpga *fpga, uint8_t addr, uint16_t value);

#endif /* _FPGA_H */
