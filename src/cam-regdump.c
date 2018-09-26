/****************************************************************************
 *  Copyright (C) 2018 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "fpga.h"
#include "utils.h"
#include "lux1310.h"

struct regtab {
    const char      *name;
    uint32_t        mask;
    unsigned int    offset;
    unsigned int    size;
    unsigned long   (*reg_read)(const struct regtab *reg, struct fpga *fpga);
    int             (*reg_write)(const struct regtab *reg, struct fpga *fpga, unsigned long val);
};

static unsigned long
fpga_reg_read(const struct regtab *r, struct fpga *fpga)
{
    switch (r->size) {
        case 1:
            return *((volatile uint8_t *)(fpga->reg) + (r->offset / sizeof(uint8_t)));
        case 2:
            return *((volatile uint16_t *)(fpga->reg) + (r->offset / sizeof(uint16_t)));
        case 4:
        default:
            return *((volatile uint32_t *)(fpga->reg) + (r->offset / sizeof(uint32_t)));
    }
} /* fpga_reg_read */

static unsigned long
sci_reg_read(const struct regtab *r, struct fpga *fpga)
{
    uint16_t first;
    int i;

    /* Set RW, address and length. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = r->offset;
    fpga->sensor->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

    if (!first && (i != 0)) {
        fprintf(stderr, "lux1310_sci_read: Read first busy was missed, address: 0x%02x\n", r->offset);
    }
    else if (i == 0) {
        fprintf(stderr, "lux1310_sci_read: Read busy not detected, something probably wrong, address: 0x%02x\n", r->offset);
    }

    usleep(1000);
    return fpga->sensor->sci_fifo_data;
}

#define REG_STRUCT(_type_, _base_, _member_) { \
    .name = #_member_, .mask = 0, \
    .offset = _base_ + OFFSET_OF(_type_, _member_), \
    .size = sizeof(((_type_ *)0)->_member_), \
    .reg_read = fpga_reg_read, \
}
#define REG_STRUCT_BIT(_type_, _base_, _member_, _bitname_, _mask_) { \
    .name = #_member_ "." _bitname_, .mask = _mask_, \
    .offset = _base_ + OFFSET_OF(_type_, _member_), \
    .size = sizeof(((_type_ *)0)->_member_), \
    .reg_read = fpga_reg_read, \
}

static const struct regtab misc_registers[] = {
    {.reg_read = fpga_reg_read, .name = "magic_delay", .offset = SENSOR_MAGIC_START_DELAY*2, .size = 2},
    {.reg_read = fpga_reg_read, .name = "line_period", .offset = SENSOR_LINE_PERIOD*2, .size = 2},
    {.reg_read = fpga_reg_read, .name = "trig_enable", .offset = TRIG_ENABLE*2, .size = 2},
    {.reg_read = fpga_reg_read, .name = "trig_invert", .offset = TRIG_INVERT*2, .size = 2},
    {.reg_read = fpga_reg_read, .name = "trig_debounce", .offset = TRIG_DEBOUNCE*2, .size = 2},
    {.reg_read = fpga_reg_read, .name = "mmu.csinv",   .offset = MMU_CONFIG*2, .size = 2, .mask = MMU_INVERT_CS},
    {.reg_read = fpga_reg_read, .name = "mmu.stuffed", .offset = MMU_CONFIG*2, .size = 2, .mask = MMU_SWITCH_STUFFED},
    {.reg_read = fpga_reg_read, .name = "version",     .offset = FPGA_VERSION*2, .size = 2},
    {.reg_read = fpga_reg_read, .name = "subversion",  .offset = FPGA_SUBVERSION*2, .size = 2},
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * FPGA/Image Sensor Control Registers
 *-------------------------------------
 */
static const struct regtab sensor_registers[] = {
    REG_STRUCT_BIT(struct fpga_sensor, SENSOR_CONTROL, control, "run", SENSOR_SCI_CONTROL_RUN_MASK),
    REG_STRUCT_BIT(struct fpga_sensor, SENSOR_CONTROL, control, "rw", SENSOR_SCI_CONTROL_RW_MASK),
    REG_STRUCT_BIT(struct fpga_sensor, SENSOR_CONTROL, control, "full", SENSOR_SCI_CONTROL_FIFO_FULL_MASK),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, clk_phase),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, sync_token),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, data_correct),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, fifo_start),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, fifo_stop),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, frame_period),
    REG_STRUCT(struct fpga_sensor, SENSOR_CONTROL, int_time),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * FPGA Video Sequencer Registers
 *-------------------------------------
 */
static const struct regtab seq_registers[] = {
    REG_STRUCT_BIT(struct fpga_seq, SEQ_CONTROL*2, control, "swtrig", SEQ_CTL_SOFTWARE_TRIG),
    REG_STRUCT_BIT(struct fpga_seq, SEQ_CONTROL*2, control, "start", SEQ_CTL_START_RECORDING),
    REG_STRUCT_BIT(struct fpga_seq, SEQ_CONTROL*2, control, "stop", SEQ_CTL_STOP_RECORDING),
    REG_STRUCT_BIT(struct fpga_seq, SEQ_CONTROL*2, control, "delay", SEQ_CTL_TRIG_DELAY_MODE),
    REG_STRUCT_BIT(struct fpga_seq, SEQ_CONTROL*2, status, "record", SEQ_STATUS_RECORDING),
    REG_STRUCT_BIT(struct fpga_seq, SEQ_CONTROL*2, status, "mdfifo", SEQ_STATUS_FIFO_EMPTY),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, frame_size),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, region_start),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, region_stop),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, live_addr[0]),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, live_addr[1]),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, live_addr[2]),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, trig_delay),
    REG_STRUCT(struct fpga_seq, SEQ_CONTROL*2, md_fifo_read),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Display Control Registers
 *-------------------------------------
 */
static const struct regtab display_registers[] = {
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "addr_sel", DISPLAY_CTL_ADDRESS_SELECT),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "scaler_nn", DISPLAY_CTL_SCALER_NN),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "sync_inh", DISPLAY_CTL_SYNC_INHIBIT),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "read_inh", DISPLAY_CTL_READOUT_INHIBIT),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "color_mode", DISPLAY_CTL_COLOR_MODE),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "focus_peak", DISPLAY_CTL_FOCUS_PEAK_ENABLE),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "focus_color", DISPLAY_CTL_FOCUS_PEAK_COLOR),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "zebra", DISPLAY_CTL_ZEBRA_ENABLE),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, control, "black_cal", DISPLAY_CTL_BLACK_CAL_MODE),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, frame_address),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, fpn_address),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, gain),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, h_period),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, v_period),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, h_sync_len),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, v_sync_len),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, h_back_porch),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, v_back_porch),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, h_res),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, v_res),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, h_out_res),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, v_out_res),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, peaking_thresh),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "bypass_fpn", DISPLAY_PIPELINE_BYPASS_FPN),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "bypass_gain", DISPLAY_PIPELINE_BYPASS_GAIN),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "bypass_demosaic", DISPLAY_PIPELINE_BYPASS_DEMOSAIC),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "bypass_ccm", DISPLAY_PIPELINE_BYPASS_COLOR_MATRIX),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "bypass_gamma", DISPLAY_PIPELINE_BYPASS_GAMMA_TABLE),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "raw_12bpp", DISPLAY_PIPELINE_RAW_12BPP),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "raw_16bpp", DISPLAY_PIPELINE_RAW_16BPP),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "raw_16pad", DISPLAY_PIPELINE_RAW_16PAD),
    REG_STRUCT_BIT(struct fpga_display, DISPLAY_CTL*2, pipeline, "test_pat", DISPLAY_PIPELINE_TEST_PATTERN),
    REG_STRUCT(struct fpga_display, DISPLAY_CTL*2, manual_sync),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Video RAM Readout Registers
 *-------------------------------------
 */
static const struct regtab vram_registers[] = {
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, identifier),
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, version),
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, subver),
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, control),
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, status),
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, address),
    REG_STRUCT(struct fpga_vram, VRAM_OFFSET, burst),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Overlay Control Registers
 *-------------------------------------
 */
static const struct regtab overlay_registers[] = {
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, identifier),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, version),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, subver),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, control),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, status),
    REG_STRUCT_BIT(struct fpga_overlay, OVERLAY_CONTROL*2, status, "textbox0", OVERLAY_CONTROL_TEXTBOX0),
    REG_STRUCT_BIT(struct fpga_overlay, OVERLAY_CONTROL*2, status, "textbox1", OVERLAY_CONTROL_TEXTBOX1),
    REG_STRUCT_BIT(struct fpga_overlay, OVERLAY_CONTROL*2, status, "watermark", OVERLAY_CONTROL_WATERMARK),
    REG_STRUCT_BIT(struct fpga_overlay, OVERLAY_CONTROL*2, status, "logomark", OVERLAY_CONTROL_LOGO_MARK),
    REG_STRUCT_BIT(struct fpga_overlay, OVERLAY_CONTROL*2, status, "fontsize", OVERLAY_CONTROL_FONT_SIZE_MASK),
    REG_STRUCT_BIT(struct fpga_overlay, OVERLAY_CONTROL*2, status, "banksel", OVERLAY_CONTROL_BANK_SELECT),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_xpos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_ypos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_xsize),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_ysize),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_xpos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_ypos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_xsize),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_ysize),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, wmark_xpos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, wmark_ypos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_xoffset),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_xoffset),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_yoffset),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_yoffset),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, logo_xpos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, logo_ypos),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, logo_xsize),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, logo_ysize),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_abgr[0]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_abgr[1]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_abgr[2]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text0_abgr[3]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_abgr[0]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_abgr[1]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_abgr[2]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, text1_abgr[3]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, wmark_abgr[0]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, wmark_abgr[1]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, wmark_abgr[2]),
    REG_STRUCT(struct fpga_overlay, OVERLAY_CONTROL*2, wmark_abgr[3]),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * LUX1310 SCI Registers
 *-------------------------------------
 */
#define REG_LUX1310(_reg_, _name_) { \
    .name = _name_, .offset = (_reg_) >> LUX1310_SCI_REG_ADDR, \
    .mask = (_reg_) & LUX1310_SCI_REG_MASK, .size = 2, \
    .reg_read = sci_reg_read \
}

static const struct regtab lux1310_registers[] = {
    REG_LUX1310(LUX1310_SCI_REV_CHIP,      "rev_chip"),
    REG_LUX1310(LUX1310_SCI_CHIP_ID,       "reg_chip_id"),
    REG_LUX1310(LUX1310_SCI_TIMING_EN,     "reg_timing_en"),
    REG_LUX1310(LUX1310_SCI_SOF_DELAY,     "reg_sof_delay"),
    REG_LUX1310(LUX1310_SCI_HBLANK,        "reg_hblank"),
    REG_LUX1310(LUX1310_SCI_ROI_NB,        "reg_roi_nb"),
    REG_LUX1310(LUX1310_SCI_ROI_EN,        "reg_roi_en"),
    REG_LUX1310(LUX1310_SCI_DRK_COL_RD,    "reg_drk_col_rd"),
    REG_LUX1310(LUX1310_SCI_VFLIP,         "reg_vflip"),
    REG_LUX1310(LUX1310_SCI_HFLIP,         "reg_hflip"),
    REG_LUX1310(LUX1310_SCI_X_START,       "reg_x_start"),
    REG_LUX1310(LUX1310_SCI_X_END,         "reg_x_end"),
    REG_LUX1310(LUX1310_SCI_Y_START,       "reg_y_start"),
    REG_LUX1310(LUX1310_SCI_Y_END,         "reg_y_end"),
    /* TODO: ROI Y/X geometry */
    REG_LUX1310(LUX1310_SCI_DRK_ROWS_ST_ADDR,  "reg_drk_rows_st_addr"),
    REG_LUX1310(LUX1310_SCI_NB_DRK_ROWS,       "reg_nb_drk_rows"),
    REG_LUX1310(LUX1310_SCI_NEXT_ROW_ADDR_OVR, "reg_next_row_addr_ovr"),
    REG_LUX1310(LUX1310_SCI_NEXT_ROW_OVR_EN,   "reg_next_row_ovr_en"),
    REG_LUX1310(LUX1310_SCI_INTER_ROI_SP,      "reg_inter_roi_sp"),
    REG_LUX1310(LUX1310_SCI_CLK_SEL_SCIP,      "reg_clk_sel_scip"),
    REG_LUX1310(LUX1310_SCI_CLK_SEL_FT,        "reg_clk_sel_ft"),
    REG_LUX1310(LUX1310_SCI_FT_TRIG_NB_PULSE,  "reg_ft_trig_nb_pulse"),
    REG_LUX1310(LUX1310_SCI_FT_RST_NB_PULSE,   "reg_ft_rst_nb_pulse"),
    REG_LUX1310(LUX1310_SCI_ABN2_EN,           "reg_abn2_en"),
    REG_LUX1310(LUX1310_SCI_RDOUT_DLY,         "reg_rdout_dly"),
    REG_LUX1310(LUX1310_SCI_ADC_CAL_EN,        "reg_adc_cal_en"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(0),         "reg_adc_os_0"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(1),         "reg_adc_os_1"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(2),         "reg_adc_os_2"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(3),         "reg_adc_os_3"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(4),         "reg_adc_os_4"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(5),         "reg_adc_os_5"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(6),         "reg_adc_os_6"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(7),         "reg_adc_os_7"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(8),         "reg_adc_os_8"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(9),         "reg_adc_os_9"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(10),         "reg_adc_os_10"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(11),         "reg_adc_os_11"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(12),         "reg_adc_os_12"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(13),         "reg_adc_os_13"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(14),         "reg_adc_os_14"),
    REG_LUX1310(LUX1310_SCI_ADC_OS(15),         "reg_adc_os_15"),
    REG_LUX1310(LUX1310_SCI_ADC_OS_SEQ_WIDTH,  "reg_adc_os_seq_width"),
    REG_LUX1310(LUX1310_SCI_PCLK_LINEVALID,    "reg_pclk_linevalid"),
    REG_LUX1310(LUX1310_SCI_PCLK_VBLANK,       "reg_pclk_vblank"),
    REG_LUX1310(LUX1310_SCI_PCLK_HBLANK,       "reg_pclk_hblank"),
    REG_LUX1310(LUX1310_SCI_PCLK_OPTICAL_BLACK,"reg_pclk_optical_black"),
    REG_LUX1310(LUX1310_SCI_MONO,              "reg_mono"),
    REG_LUX1310(LUX1310_SCI_ROW2EN,            "reg_row2en"),
    REG_LUX1310(LUX1310_SCI_POUTSEL,           "reg_poutsel"),
    REG_LUX1310(LUX1310_SCI_INVERT_ANALOG,     "reg_invert_analog"),
    REG_LUX1310(LUX1310_SCI_GLOBAL_SHUTTER,    "reg_global_shutter"),
    REG_LUX1310(LUX1310_SCI_GAIN_SEL_SAMP,     "reg_gain_sel_samp"),
    REG_LUX1310(LUX1310_SCI_GAIN_SEL_FB,       "reg_gain_sel_fb"),
    REG_LUX1310(LUX1310_SCI_GAIN_BIT,          "reg_gain_bit"),
    REG_LUX1310(LUX1310_SCI_COLBIN2,           "reg_colbin2"),
    REG_LUX1310(LUX1310_SCI_TST_PAT,           "reg_tst_pat"),
    REG_LUX1310(LUX1310_SCI_CUST_PAT,          "reg_cust_pat"),
    REG_LUX1310(LUX1310_SCI_MUX_MODE,          "reg_mux_mode"),
    REG_LUX1310(LUX1310_SCI_PWR_EN_SERIALIZER_B, "reg_pwr_en_serializer_b"),
    REG_LUX1310(LUX1310_SCI_DAC_ILV,           "reg_dac_ilv"),
    REG_LUX1310(LUX1310_SCI_MSB_FIRST_DATA,    "reg_msb_first_data"),
    REG_LUX1310(LUX1310_SCI_PCLK_INV,          "reg_pclk_inv"),
    REG_LUX1310(LUX1310_SCI_TERMB_DATA,        "reg_termb_data"),
    REG_LUX1310(LUX1310_SCI_DCLK_INV,          "reg_dclk_inv"),
    REG_LUX1310(LUX1310_SCI_TERMB_CLK,         "reg_termb_clk"),
    REG_LUX1310(LUX1310_SCI_TERMB_RXCLK,       "reg_termb_rxclk"),
    REG_LUX1310(LUX1310_SCI_PWREN_DCLK_B,      "reg_pwren_dclk_b"),
    REG_LUX1310(LUX1310_SCI_PWREN_PCLK_B,      "reg_pwren_pclk_b"),
    REG_LUX1310(LUX1310_SCI_PWREN_BIAS_B,      "reg_pwren_bias_b"),
    REG_LUX1310(LUX1310_SCI_PWREN_DRV_B,       "reg_pwren_drv_b"),
    REG_LUX1310(LUX1310_SCI_PWREN_ADC_B,       "reg_pwren_adc_b"),
    REG_LUX1310(LUX1310_SCI_SEL_VCMI,          "reg_sel_vcmi"),
    REG_LUX1310(LUX1310_SCI_SEL_VCMO,          "reg_sel_vcmo"),
    REG_LUX1310(LUX1310_SCI_SEL_VCMP,          "reg_sel_vcmp"),
    REG_LUX1310(LUX1310_SCI_SEL_VCMN,          "reg_sel_vcmn"),
    REG_LUX1310(LUX1310_SCI_HIDY_EN,           "reg_hidy_en"),
    REG_LUX1310(LUX1310_SCI_HIDY_TRIG_NB_PULSE,"reg_hidy_trig_nb_pulse"),
    REG_LUX1310(LUX1310_SCI_SEL_VDR1_WIDTH,    "reg_vdr1_width"),
    REG_LUX1310(LUX1310_SCI_SEL_VDR2_WIDTH,    "reg_vdr2_width"),
    REG_LUX1310(LUX1310_SCI_SEL_VDR3_WIDTH,    "reg_vdr3_width"),
    REG_LUX1310(LUX1310_SCI_SER_SYNC,          "reg_ser_sync"),
    REG_LUX1310(LUX1310_SCI_CLK_SYNC,          "reg_clk_sync"),
    REG_LUX1310(LUX1310_SCI_SRESET_B,          "reg_sreset_b"),
    {NULL, 0, 0, 0}
};

static void
print_reg_group(FILE *fp, const struct regtab *regs, struct fpga *fpga, const char *groupname)
{
    const char *header = "\t%-6s  %-10s  %24s  %s\n";
    const char *format = "\t0x%04x  0x%08x  %24s  0x%x\n";
    int i;

    if (groupname) fprintf(fp, "\n%s Registers:\n", groupname);
    fprintf(fp, header, "ADDR", "MASK", "NAME", "VALUE");

    for (i = 0; regs[i].name; i++) {
        unsigned long value = regs[i].reg_read(&regs[i], fpga);
        uint32_t mask = (regs[i].mask) ? (regs[i].mask) : UINT32_MAX;
        fprintf(fp, format, regs[i].offset, mask, regs[i].name, getbits(value, mask));
    } /* for */
} /* print_reg_group */

int
main(int argc, char *const argv[])
{
    struct fpga *fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA register space: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Pretty Print the registers. */
    print_reg_group(stdout, misc_registers, fpga, NULL);
    print_reg_group(stdout, sensor_registers, fpga, "Sensor");
    print_reg_group(stdout, seq_registers, fpga, "Sequencer");
    print_reg_group(stdout, display_registers, fpga, "Display");
    print_reg_group(stdout, vram_registers, fpga, "Video RAM");
    print_reg_group(stdout, overlay_registers, fpga, "Overlay");
    print_reg_group(stdout, lux1310_registers, fpga, "LUX1310");

    fpga_close(fpga);
    return 0;
} /* main */
