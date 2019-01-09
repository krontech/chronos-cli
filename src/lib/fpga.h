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

/* FPGA Timing clock runs at 100MHz */
#define FPGA_TIMEBASE_HZ            100000000

#define FPGA_FRAME_WORD_SIZE        32

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
    uint32_t sci_fifo_write;
    uint32_t sci_fifo_read;
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
    uint32_t __reserved0[3];
    uint32_t v_out_res;
    uint32_t peaking_thresh;
    uint32_t pipeline;
    uint32_t __reserved1[1];
    uint32_t manual_sync;
};

/* Color Correction and White Balance */
struct fpga_color {
    uint32_t ccm_red[3];
    uint32_t __reserved0[1];
    uint32_t ccm_green[3];
    uint32_t ccm_blue[3];
    uint32_t __reserved1[2];
    uint32_t wbal_red;
    uint32_t wbal_green;
    uint32_t wbal_blue;
};

/* Video RAM readout */
struct fpga_vram {
    uint32_t identifier;    /* Should always read 0x0040 */
    uint32_t version;
    uint32_t subver;
    uint32_t control;
    uint32_t status;
    uint32_t __reserved0[3];
    uint32_t address;
    uint32_t burst;
    uint8_t __reserved1[0x200 - 0x28]; /* Align to offset 0x200 */
    uint16_t buffer[1024];
};
#define VRAM_IDENTIFIER     0x40

#define VRAM_CTL_TRIG_READ  (1 << 0)
#define VRAM_CTL_TRIG_WRITE (1 << 1)

struct fpga_overlay {
    /* Overlay control registers. */
    uint16_t identifier;    /* Always reads 0x0055 */
    uint16_t version;
    uint16_t subver;
    uint16_t control;
    uint16_t status;
    uint16_t text0_xpos;
    uint16_t text0_ypos;
    uint16_t text0_xsize;
    uint16_t text0_ysize;
    uint16_t text1_xpos;
    uint16_t text1_ypos;
    uint16_t text1_xsize;
    uint16_t text1_ysize;
    uint16_t wmark_xpos;
    uint16_t wmark_ypos;
    uint8_t  text0_xoffset;
    uint8_t  text1_xoffset;
    uint8_t  text0_yoffset;
    uint8_t  text1_yoffset;
    uint16_t logo_xpos;
    uint16_t logo_ypos;
    uint16_t logo_xsize;
    uint16_t logo_ysize;
    uint8_t  text0_abgr[4];
    uint8_t  text1_abgr[4];
    uint8_t  wmark_abgr[4];
    uint8_t __reserved0[0x8100 - 0x8036]; /* Align to offset 0x100 */

    /* Text buffer registers. */
#define OVERLAY_TEXT_LENGTH     128
    uint8_t text0_buffer[OVERLAY_TEXT_LENGTH];
    uint8_t __reserved1[128];
    uint8_t text1_buffer[OVERLAY_TEXT_LENGTH];
    uint8_t __reserved2[128];
    uint8_t logo_red_lut[256];
    uint8_t logo_green_lut[256];
    uint8_t logo_blue_lut[256];
    uint8_t __reserved3[0x9000 - 0x8600]; /* Align to offset 0x1000 */

    /* Bitmap display registers */
    uint16_t text0_bitmap[4096];
    uint16_t text0_reserved[2048];
    uint16_t text1_bitmap[4096];
    uint16_t text1_reserved[2048];
    uint16_t logo[20480];
};

/* Recording Segment Data Entry */
struct fpga_segment_entry {
    uint32_t start;
    uint32_t end;
    uint32_t last;
    uint32_t data;
};

/* Recording Segment Readout */
struct fpga_segments {
    uint32_t identifier;    /* Should always read 0x0040 */
    uint32_t version;
    uint32_t subver;
    uint32_t control;
    uint32_t status;
    uint32_t __reserved0[3];
    uint32_t blockno;
    uint32_t __reserved1[504];
    /* Aligned to offset 0x800 */
    struct fpga_segment_entry data[128];
};

#define SEGMENT_CONTROL_RESET   (1 << 0)    /* Erase the segment data. */

#define SEGMENT_DATA_PC         0xf0000000  /* Sequencer program counter */
#define SEGMENT_DATA_BLOCKNO    0x00ffffff  /* Segment block number. */

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

#define WBAL_RED                    0x278
#define WBAL_GREEN                  0x27A
#define WBAL_BLUE                   0x27C

#define WL_DYNDLY_0					0x280
#define WL_DYNDLY_1					0x282
#define WL_DYNDLY_2					0x284
#define WL_DYNDLY_3					0x286

#define MMU_CONFIG                  0x290

#define	SYSTEM_RESET				0x300
#define FPGA_VERSION				0x302
#define FPGA_SUBVERSION             0x304
#define	DCG_MEM_START				0x800
#define VRAM_OFFSET                 0x1000

#define OVERLAY_CONTROL             0x4000

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
#define SENSOR_SCI_CONTROL_FIFO_RESET_MASK  0x8000              //Write 1 to reset the FIFO

#define	EXT_SH_TRIGD_EXP_EN_MASK		0x1
#define	EXT_SH_TRIGD_EXP_EN_OFFSET		0
#define	EXT_SH_GATING_EN_MASK			0x2
#define	EXT_SH_GATING_EN_OFFSET			1
#define	EXT_SH_SRC_EN_MASK				0x1C
#define	EXT_SH_SRC_EN_OFFSET			2

#define DISPLAY_CTL_ADDRESS_SELECT      (1 << 0)
#define DISPLAY_CTL_SCALER_NN           (1 << 1)
#define DISPLAY_CTL_SYNC_INHIBIT        (1 << 2)
#define DISPLAY_CTL_READOUT_INHIBIT     (1 << 3)
#define DISPLAY_CTL_COLOR_MODE          (1 << 4)
#define DISPLAY_CTL_FOCUS_PEAK_ENABLE   (1 << 5)
#define DISPLAY_CTL_FOCUS_PEAK_COLOR    (0x7 << 6)
#define DISPLAY_CTL_ZEBRA_ENABLE        (1 << 9)
#define DISPLAY_CTL_BLACK_CAL_MODE      (1 << 10)

#define DISPLAY_PIPELINE_BYPASS_FPN         (1<<0)
#define DISPLAY_PIPELINE_BYPASS_GAIN        (1<<1)
#define DISPLAY_PIPELINE_BYPASS_DEMOSAIC    (1<<2)
#define DISPLAY_PIPELINE_BYPASS_COLOR_MATRIX (1<<3)
#define DISPLAY_PIPELINE_BYPASS_GAMMA_TABLE (1<<4)
#define DISPLAY_PIPELINE_RAW_12BPP          (1<<5)
#define DISPLAY_PIPELINE_RAW_16BPP          (1<<6)
#define DISPLAY_PIPELINE_RAW_16PAD          (1<<7)
#define DISPLAY_PIPELINE_TEST_PATTERN       (1<<15)
#define DISPLAY_PIPELINE_RAW_MODES          (0x7 << 5)

#define SEQ_CTL_SOFTWARE_TRIG               (1 << 0)
#define SEQ_CTL_START_RECORDING             (1 << 1)
#define SEQ_CTL_STOP_RECORDING              (1 << 2)
#define SEQ_CTL_TRIG_DELAY_MODE             (1 << 3) /* an undocumented mystery! */

#define SEQ_STATUS_RECORDING                (1 << 0)
#define SEQ_STATUS_FIFO_EMPTY               (1 << 1)

#define OVERLAY_CONTROL_TEXTBOX0            (1 << 4)
#define OVERLAY_CONTROL_TEXTBOX1            (1 << 5)
#define OVERLAY_CONTROL_WATERMARK           (1 << 6)
#define OVERLAY_CONTROL_LOGO_MARK           (1 << 7)
#define OVERLAY_CONTROL_FONT_SIZE(_x_)      ((_x_) << 8)
#define OVERLAY_CONTROL_FONT_SIZE_MASK      (0x3F << 8)
#define OVERLAY_CONTROL_BANK_SELECT         (1 << 15)

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
    volatile struct fpga_vram   *vram; 
    volatile struct fpga_overlay *overlay;
    volatile uint32_t           *cc_matrix;
};

struct fpga *fpga_open(void);
void fpga_close(struct fpga *fpga);
int fpga_load(const char *spi, const char *bitstream, FILE *log);

#endif /* _FPGA_H */
