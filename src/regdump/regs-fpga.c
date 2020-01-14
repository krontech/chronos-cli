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
#include "regs.h"
#include "utils.h"

unsigned long
fpga_reg_read(const struct regdef *r, struct fpga *fpga)
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
const struct regdef sensor_regdefs[] = {
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
const struct reggroup sensor_registers = {
    .name = "Sensor",
    .filter = "sensor",
    .rtab = sensor_regdefs
};

/*-------------------------------------
 * FPGA Video Sequencer Registers
 *-------------------------------------
 */
const struct regdef seq_regdefs[] = {
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
const struct reggroup seq_registers = {
    .name = "Sequencer",
    .filter = "seq",
    .rtab = seq_regdefs
};

/*-------------------------------------
 * Trigger and IO Registers
 *-------------------------------------
 */
const struct regdef trigger_regdefs[] = {
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
const struct reggroup trigger_registers = {
    .name = "Trigger",
    .filter = "trigger",
    .rtab = trigger_regdefs
};

/*-------------------------------------
 * Display Control Registers
 *-------------------------------------
 */
const struct regdef display_regdefs[] = {
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
const struct reggroup display_registers = {
    .name = "Display",
    .filter = "display",
    .rtab = display_regdefs
};

/*-------------------------------------
 * Configuration and Misc Registers
 *-------------------------------------
 */
const struct regdef config_regdefs[] = {
    REG_STRUCT_BIT(struct fpga_config, FPGA_CONFIG_BASE, mmu_config, "csinv", MMU_INVERT_CS),
    REG_STRUCT_BIT(struct fpga_config, FPGA_CONFIG_BASE, mmu_config, "stuffed", MMU_SWITCH_STUFFED),
    REG_STRUCT(struct fpga_config, FPGA_CONFIG_BASE, version),
    REG_STRUCT(struct fpga_config, FPGA_CONFIG_BASE, subver),
    {NULL, 0, 0, 0}
};
const struct reggroup config_registers = {
    .name = "Config",
    .filter = "config",
    .rtab = config_regdefs
};

/*-------------------------------------
 * Video RAM Readout Registers
 *-------------------------------------
 */
const struct regdef vram_regdefs[] = {
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, identifier),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, version),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, subver),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, control),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, status),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, address),
    REG_STRUCT(struct fpga_vram, FPGA_VRAM_BASE, burst),
    {NULL, 0, 0, 0}
};
const struct reggroup vram_registers = {
    .name = "Video RAM",
    .filter = "vram",
    .rtab = vram_regdefs
};

/*-------------------------------------
 * Sequencer Program Registers
 *-------------------------------------
 */
const struct regdef seqpgm_regdefs[] = {
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, identifier),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, version),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, subver),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, control),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, status),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, frame_write),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, frame_read),
    REG_STRUCT(struct fpga_seqpgm, FPGA_SEQPGM_BASE, block_count),
    {NULL, 0, 0, 0}
};
const struct reggroup seqpgm_registers = {
    .name = "Sequencer Program",
    .filter = "seqpgm",
    .rtab = seqpgm_regdefs
};

/*-------------------------------------
 * Imager Control Registers
 *-------------------------------------
 */
const struct regdef imager_regdefs[] = {
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
const struct reggroup imager_registers = {
    .name = "Imager",
    .filter = "imager",
    .rtab = imager_regdefs
};

/*-------------------------------------
 * Timing Control Registers
 *-------------------------------------
 */
const struct regdef timing_regdefs[] = {
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, identifier),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, version),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, subver),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, status, "active_page", TIMING_STATUS_ACTIVE_PAGE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, status, "exposure_enable", TIMING_STATUS_EXPOSURE_ENABLE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, status, "frame_request", TIMING_STATUS_FRAME_REQUEST),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, status, "wait_trig_active", TIMING_STATUS_WAIT_TRIG_ACTIVE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, status, "wait_trig_inactive", TIMING_STATUS_WAIT_TRIG_INACTIVE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, status, "page_swap_state", TIMING_STATUS_PAGE_SWAP_STATE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "inhibit", TIMING_CONTROL_INHIBIT),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "request_flip", TIMING_CONTROL_REQUEST_FLIP),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "soft_reset", TIMING_CONTROL_SOFT_RESET),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "exposure_enable", TIMING_CONTROL_EXPOSURE_ENABLE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "exposure_request", TIMING_CONTROL_EXPOSURE_REQUEST),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "frame_request", TIMING_CONTROL_FRAME_REQUEST),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "abn_pulse_enable", TIMING_CONTROL_ABN_PULSE_ENABLE),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "abn_pulse_invert", TIMING_CONTROL_ABN_PULSE_INVERT),
    REG_STRUCT_BIT(struct fpga_timing, FPGA_TIMING_BASE, control, "wavetable_latch", TIMING_CONTROL_WAVETABLE_LATCH),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, abn_pulse_low),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, abn_pulse_high),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, min_lines),
    /* The following registers are available in version 1 and higher. */
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, period_time),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, exp_abn_time),
    REG_STRUCT(struct fpga_timing, FPGA_TIMING_BASE, exp_abn2_time),
    /* TODO: uint32_t operands[8]; */
    /* TODO: decompile the program? */
    {NULL, 0, 0, 0}
};
const struct reggroup timing_registers = {
    .name = "Timing",
    .filter = "timing",
    .rtab = timing_regdefs
};

/*-------------------------------------
 * Overlay Control Registers
 *-------------------------------------
 */
const struct regdef overlay_regdefs[] = {
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
const struct reggroup overlay_registers = {
    .name = "Overlay",
    .filter = "overlay",
    .rtab = overlay_regdefs
};

/*-------------------------------------
 * Zebra Control Registers
 *-------------------------------------
 */
const struct regdef zebra_regdefs[] = {
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, identifier),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, version),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, subver),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, status),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, control),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, __reserved0[0]),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, __reserved0[1]),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, __reserved0[2]),
    REG_STRUCT(struct fpga_zebra, FPGA_ZEBRA_BASE, threshold),
    {NULL, 0, 0, 0}
};
const struct reggroup zebra_registers = {
    .name = "Zebra",
    .filter = "zebra",
    .rtab = zebra_regdefs
};
