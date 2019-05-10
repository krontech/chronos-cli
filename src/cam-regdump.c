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
#include "lux2100.h"

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
    return fpga->sensor->sci_fifo_read;
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

/*-------------------------------------
 * FPGA/Image Sensor Control Registers
 *-------------------------------------
 */
static const struct regtab sensor_registers[] = {
    REG_STRUCT_BIT(struct fpga_sensor, FPGA_SENSOR_BASE, control, "run", SENSOR_SCI_CONTROL_RUN_MASK),
    REG_STRUCT_BIT(struct fpga_sensor, FPGA_SENSOR_BASE, control, "rw", SENSOR_SCI_CONTROL_RW_MASK),
    REG_STRUCT_BIT(struct fpga_sensor, FPGA_SENSOR_BASE, control, "full", SENSOR_SCI_CONTROL_FULL_MASK),
    REG_STRUCT_BIT(struct fpga_sensor, FPGA_SENSOR_BASE, control, "reset", SENSOR_SCI_CONTROL_RESET_MASK),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, clk_phase),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, sync_token),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, data_correct),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, fifo_start),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, fifo_stop),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, frame_period),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, int_time),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, start_delay),
    REG_STRUCT(struct fpga_sensor, FPGA_SENSOR_BASE, line_period),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * FPGA Video Sequencer Registers
 *-------------------------------------
 */
static const struct regtab seq_registers[] = {
    REG_STRUCT_BIT(struct fpga_seq, FPGA_SEQUENCER_BASE, control, "swtrig", SEQ_CTL_SOFTWARE_TRIG),
    REG_STRUCT_BIT(struct fpga_seq, FPGA_SEQUENCER_BASE, control, "start", SEQ_CTL_START_RECORDING),
    REG_STRUCT_BIT(struct fpga_seq, FPGA_SEQUENCER_BASE, control, "stop", SEQ_CTL_STOP_RECORDING),
    REG_STRUCT_BIT(struct fpga_seq, FPGA_SEQUENCER_BASE, control, "delay", SEQ_CTL_TRIG_DELAY_MODE),
    REG_STRUCT_BIT(struct fpga_seq, FPGA_SEQUENCER_BASE, status, "record", SEQ_STATUS_RECORDING),
    REG_STRUCT_BIT(struct fpga_seq, FPGA_SEQUENCER_BASE, status, "mdfifo", SEQ_STATUS_FIFO_EMPTY),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, frame_size),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, region_start),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, region_stop),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, live_addr[0]),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, live_addr[1]),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, live_addr[2]),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, trig_delay),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, md_fifo_read),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, write_addr),
    REG_STRUCT(struct fpga_seq, FPGA_SEQUENCER_BASE, last_addr),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Trigger and IO Registers
 *-------------------------------------
 */
static const struct regtab trigger_registers[] = {
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, enable),
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, invert),
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, debounce),
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, io_output),
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, io_source),
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, io_invert),
    REG_STRUCT(struct fpga_trigger, FPGA_TRIGGER_BASE, io_input),
    REG_STRUCT_BIT(struct fpga_trigger, FPGA_TRIGGER_BASE, ext_shutter, "exposure", EXT_SHUTTER_EXPOSURE_ENABLE),
    REG_STRUCT_BIT(struct fpga_trigger, FPGA_TRIGGER_BASE, ext_shutter, "gating", EXT_SHUTTER_GATING_ENABLE),
    REG_STRUCT_BIT(struct fpga_trigger, FPGA_TRIGGER_BASE, ext_shutter, "source", EXT_SHUTTER_SOURCE_MASK),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Display Control Registers
 *-------------------------------------
 */
static const struct regtab display_registers[] = {
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "addr_sel", DISPLAY_CTL_ADDRESS_SELECT),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "scaler_nn", DISPLAY_CTL_SCALER_NN),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "sync_inh", DISPLAY_CTL_SYNC_INHIBIT),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "read_inh", DISPLAY_CTL_READOUT_INHIBIT),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "color_mode", DISPLAY_CTL_COLOR_MODE),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "focus_peak", DISPLAY_CTL_FOCUS_PEAK_ENABLE),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "focus_color", DISPLAY_CTL_FOCUS_PEAK_COLOR),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "zebra", DISPLAY_CTL_ZEBRA_ENABLE),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, control, "black_cal", DISPLAY_CTL_BLACK_CAL_MODE),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, frame_address),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, fpn_address),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, gain),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, h_period),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, v_period),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, h_sync_len),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, v_sync_len),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, h_back_porch),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, v_back_porch),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, h_res),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, v_res),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, h_out_res),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, v_out_res),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, peaking_thresh),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "bypass_fpn", DISPLAY_PIPELINE_BYPASS_FPN),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "bypass_gain", DISPLAY_PIPELINE_BYPASS_GAIN),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "bypass_demosaic", DISPLAY_PIPELINE_BYPASS_DEMOSAIC),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "bypass_ccm", DISPLAY_PIPELINE_BYPASS_COLOR_MATRIX),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "bypass_gamma", DISPLAY_PIPELINE_BYPASS_GAMMA_TABLE),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "raw_12bpp", DISPLAY_PIPELINE_RAW_12BPP),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "raw_16bpp", DISPLAY_PIPELINE_RAW_16BPP),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "raw_16pad", DISPLAY_PIPELINE_RAW_16PAD),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, pipeline, "test_pat", DISPLAY_PIPELINE_TEST_PATTERN),
    REG_STRUCT(struct fpga_display, FPGA_DISPLAY_BASE, manual_sync),
    REG_STRUCT_BIT(struct fpga_display, FPGA_DISPLAY_BASE, gainctl, "3point", DISPLAY_GAINCTL_3POINT),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Configuration and Misc Registers
 *-------------------------------------
 */
static const struct regtab config_registers[] = {
    REG_STRUCT_BIT(struct fpga_config, FPGA_CONFIG_BASE, mmu_config, "csinv", MMU_INVERT_CS),
    REG_STRUCT_BIT(struct fpga_config, FPGA_CONFIG_BASE, mmu_config, "stuffed", MMU_SWITCH_STUFFED),
    REG_STRUCT(struct fpga_config, FPGA_CONFIG_BASE, version),
    REG_STRUCT(struct fpga_config, FPGA_CONFIG_BASE, subver),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Video RAM Readout Registers
 *-------------------------------------
 */
static const struct regtab vram_registers[] = {
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, identifier),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, version),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, subver),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, control),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, status),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, address),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, burst),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Imager Control Registers
 *-------------------------------------
 */
static const struct regtab imager_registers[] = {
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, identifier),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, version),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, subver),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, status),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, control),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, clk_phase),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, sync_token),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, fifo_start),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, fifo_stop),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, hres_count),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, vres_count),
    REG_STRUCT(struct fpga_imager, FPGA_IMAGER_BASE, data_correct),
    {NULL, 0, 0, 0}
};

/*-------------------------------------
 * Overlay Control Registers
 *-------------------------------------
 */
static const struct regtab overlay_registers[] = {
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, identifier),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, version),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, subver),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, control),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, status),
    REG_STRUCT_BIT(struct fpga_overlay, FPGA_OVERLAY_BASE, status, "textbox0", OVERLAY_CONTROL_TEXTBOX0),
    REG_STRUCT_BIT(struct fpga_overlay, FPGA_OVERLAY_BASE, status, "textbox1", OVERLAY_CONTROL_TEXTBOX1),
    REG_STRUCT_BIT(struct fpga_overlay, FPGA_OVERLAY_BASE, status, "watermark", OVERLAY_CONTROL_WATERMARK),
    REG_STRUCT_BIT(struct fpga_overlay, FPGA_OVERLAY_BASE, status, "logomark", OVERLAY_CONTROL_LOGO_MARK),
    REG_STRUCT_BIT(struct fpga_overlay, FPGA_OVERLAY_BASE, status, "fontsize", OVERLAY_CONTROL_FONT_SIZE_MASK),
    REG_STRUCT_BIT(struct fpga_overlay, FPGA_OVERLAY_BASE, status, "banksel", OVERLAY_CONTROL_BANK_SELECT),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_xpos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_ypos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_xsize),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_ysize),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_xpos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_ypos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_xsize),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_ysize),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, wmark_xpos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, wmark_ypos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_xoffset),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_xoffset),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_yoffset),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_yoffset),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, logo_xpos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, logo_ypos),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, logo_xsize),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, logo_ysize),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_abgr[0]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_abgr[1]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_abgr[2]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text0_abgr[3]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_abgr[0]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_abgr[1]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_abgr[2]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, text1_abgr[3]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, wmark_abgr[0]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, wmark_abgr[1]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, wmark_abgr[2]),
    REG_STRUCT(struct fpga_overlay, FPGA_OVERLAY_BASE, wmark_abgr[3]),
    {NULL, 0, 0, 0}
};


/*-------------------------------------
 * Generic Luxima Chip ID registers.
 *-------------------------------------
 */
static const struct regtab luxima_chipid_registers[] = {
    {
        .name = "chip_id",
        .offset = 0x00,
        .mask = 0xff00,
        .size = 2,
        .reg_read = sci_reg_read,
    },
    {
        .name = "rev_chip",
        .offset = 0x00,
        .mask = 0x00ff,
        .size = 2,
        .reg_read = sci_reg_read,
    },
    {NULL, 0, 0, 0}
};

static unsigned int
sci_read_chipid(struct fpga *fpga)
{
    const struct regtab *r = &luxima_chipid_registers[0];
    return getbits(sci_reg_read(r, fpga), r->mask);
}

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

/*-------------------------------------
 * LUX2100/LUX8M SCI Registers
 *-------------------------------------
 */
#define REG_LUX2100(_reg_, _name_) { \
    .name = _name_, .offset = (_reg_) >> LUX2100_SCI_REG_ADDR, \
    .mask = (_reg_) & LUX2100_SCI_REG_MASK, .size = 2, \
    .reg_read = sci_reg_read \
}

static const struct regtab lux2100_sensor_registers[] = {
    REG_LUX2100(LUX2100_SCI_REV_CHIP,       "rev_chip"),
    REG_LUX2100(LUX2100_SCI_CHIP_ID,        "chip_id"),
    REG_LUX2100(LUX2100_SCI_TIMING_EN,      "timing_en"),
    REG_LUX2100(LUX2100_SCI_GLOBAL_SHUTTER, "global_shutter"),
    REG_LUX2100(LUX2100_SCI_FTR_EN,         "ftr_en"),
    REG_LUX2100(LUX2100_SCI_FT_RST_EN,      "ft_rst_en"),
    REG_LUX2100(LUX2100_SCI_ROI_SEL,        "roi_sel"),
    REG_LUX2100(LUX2100_SCI_INTER_ROI_SP,   "inter_roi_sp"),
    REG_LUX2100(LUX2100_SCI_DRK_COL_RD,     "drk_col_rd"),
    REG_LUX2100(LUX2100_SCI_VFLIP,          "vflip"),
    REG_LUX2100(LUX2100_SCI_HFLIP,          "hflip"),
    REG_LUX2100(LUX2100_SCI_HBLANK,         "hblank"),
    REG_LUX2100(LUX2100_SCI_DP_SCIP_EN,     "dp_scip_en"),
    REG_LUX2100(LUX2100_SCI_SHADOW_REGS,        "shadow_regs"),
    REG_LUX2100(LUX2100_SCI_X_START,            "x_start"),
    REG_LUX2100(LUX2100_SCI_X_END,              "x_end"),
    REG_LUX2100(LUX2100_SCI_Y_START,            "y_start"),
    REG_LUX2100(LUX2100_SCI_Y_END,              "y_end"),
    REG_LUX2100(LUX2100_SCI_DRK_ROWS_ST_ADDR_TOP,   "drk_rows_st_addr_top"),
    REG_LUX2100(LUX2100_SCI_NB_DRK_ROWS_TOP,    "nb_drk_rows_top"),
    REG_LUX2100(LUX2100_SCI_NEXT_ROW_ADDR_OVR,  "next_row_addr_ovr"),
    REG_LUX2100(LUX2100_SCI_NEXT_ROW_OVR_EN,    "next_row_ovr_en"),
    REG_LUX2100(LUX2100_SCI_SOF_DELAY,          "sof_delay"),
    REG_LUX2100(LUX2100_SCI_FT_TRIG_NB_PULSE,   "ft_trig_nb_pulse"),
    REG_LUX2100(LUX2100_SCI_FT_RST_NB_PULSE,    "ft_rst_nb_pulse"),
    REG_LUX2100(LUX2100_SCI_RDOUT_DLY,          "rdout_dly"),
    REG_LUX2100(LUX2100_SCI_ROW_TIME,           "row_time"),
    REG_LUX2100(LUX2100_SCI_ABN_SEL,            "abn_sel"),
    REG_LUX2100(LUX2100_SCI_ABN2_EN,            "abn2_en"),
    REG_LUX2100(LUX2100_SCI_ABN2_ALT_PAT,       "abn2_alt_pat"),
    REG_LUX2100(LUX2100_SCI_ABN2_LD,            "abn2_ld"),
    REG_LUX2100(LUX2100_SCI_X_ORG,              "x_org"),
    REG_LUX2100(LUX2100_SCI_Y_ORG,              "y_org"),
    REG_LUX2100(LUX2100_SCI_PCLK_LINEVALID,     "pclk_linevalid"),
    REG_LUX2100(LUx2100_SCI_PCLK_VBLANK,        "pclk_vblank"),
    REG_LUX2100(LUX2100_SCI_PCLK_HBLANK,        "pclk_hblank"),
    REG_LUX2100(LUX2100_SCI_PCLK_OPTICAL_BLACK, "pclk_optical_black"),
    REG_LUX2100(LUX2100_SCI_MONO,               "mono"),
    REG_LUX2100(LUX2100_SCI_ROWBIN2,            "rowbin2"),
    REG_LUX2100(LUX2100_SCI_ROW2EN,             "row2en"),
    REG_LUX2100(LUX2100_SCI_COLBIN2,            "colbin2"),
    REG_LUX2100(LUX2100_SCI_COLBIN4,            "colbin4"),
    REG_LUX2100(LUX2100_SCI_POUTSEL,            "poutsel"),
    REG_LUX2100(LUX2100_SCI_INVERT_ANALOG,      "invert_analog"),
    REG_LUX2100(LUX2100_SCI_GAIN_SEL_SAMP,      "gain_sel_samp"),
    REG_LUX2100(LUX2100_SCI_GAIN_SEL_FB,        "gain_sel_fb"),
    REG_LUX2100(LUX2100_SCI_GAIN_SERIAL,        "gain_serial"),
    REG_LUX2100(LUX2100_SCI_LV_DELAY,           "lv_delay"),
    REG_LUX2100(LUX2100_SCI_CUST_PAT,           "cust_pat"),
    REG_LUX2100(LUX2100_SCI_TST_PAT,            "tst_pat"),
    REG_LUX2100(LUX2100_SCI_PWR_EN_SERIALIZER_B, "pwr_en_serializer_b"),
    REG_LUX2100(LUX2100_SCI_MUX_MODE,           "mux_mod"),
    REG_LUX2100(LUX2100_SCI_DAC_ILV,            "dac_ilv"),
    REG_LUX2100(LUX2100_SCI_PCLK_INV,           "pclk_inv"),
    REG_LUX2100(LUX2100_SCI_DCLK_INV,           "dclk_inv"),
    REG_LUX2100(LUX2100_SCI_TERMB_DATA,         "termb_data"),
    REG_LUX2100(LUX2100_SCI_TERMB_CLK,          "termb_clk"),
    REG_LUX2100(LUX2100_SCI_SEL_VLNKEEP_RST,    "sel_vlnkeep_rst"),
    REG_LUX2100(LUX2100_SCI_SEL_VDUM,           "sel_vdum"),
    REG_LUX2100(LUX2100_SCI_SEL_VDUMRST,        "sel_vdumrst"),
    REG_LUX2100(LUX2100_SCI_SEL_VLNKEEP,        "sel_vlnkeep"),
    REG_LUX2100(LUX2100_SCI_HIDY_EN,            "hidy_en"),
    REG_LUX2100(LUX2100_SCI_GLB_FLUSH_EN,       "glb_flush_en"),
    REG_LUX2100(LUX2100_SCI_SEL_VDR1_WIDTH,     "sel_vdr1_width"),
    REG_LUX2100(LUX2100_SCI_SEL_VDR2_WIDTH,     "sel_vdr2_width"),
    REG_LUX2100(LUX2100_SCI_SEL_VDR3_WIDTH,     "sel_vdr3_width"),
    REG_LUX2100(LUX2100_SCI_ICOL_CAP_EN,        "icol_cap_en"),
    REG_LUX2100(LUX2100_SCI_SRESET_B,           "sreset_b"),
    REG_LUX2100(LUX2100_SCI_SER_SYNC,           "ser_sync"),
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
    unsigned int chipid;
    struct fpga *fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA register space: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Pretty Print the registers. */
    print_reg_group(stdout, sensor_registers, fpga, "Sensor");
    print_reg_group(stdout, seq_registers, fpga, "Sequencer");
    print_reg_group(stdout, trigger_registers, fpga, "Trigger");
    print_reg_group(stdout, display_registers, fpga, "Display");
    print_reg_group(stdout, config_registers, fpga, "Config");
    print_reg_group(stdout, vram_registers, fpga, "Video RAM");
    print_reg_group(stdout, overlay_registers, fpga, "Overlay");
    print_reg_group(stdout, imager_registers, fpga, "Imager");

    /* Read the Luxima chip ID. */
    chipid = sci_read_chipid(fpga);
    if (chipid == LUX1310_CHIP_ID) {
        print_reg_group(stdout, lux1310_registers, fpga, "LUX1310");
    }
    else if (chipid == LUX2100_CHIP_ID) {
        print_reg_group(stdout, lux2100_sensor_registers, fpga, "LUX2100 Sensor");
        /* TODO: LUX2100 datapath registers. */
    }
    else {
        print_reg_group(stdout, luxima_chipid_registers, fpga, "Unknown Sensor");
    }

    fpga_close(fpga);
    return 0;
} /* main */
