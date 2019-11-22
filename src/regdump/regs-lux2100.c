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
#include "lux2100.h"

#define REG_LUX2100(_reg_, _name_) { \
    .name = _name_, .offset = (_reg_) >> LUX2100_SCI_REG_ADDR, \
    .mask = (_reg_) & LUX2100_SCI_REG_MASK, .size = 2, \
    .reg_read = sci_reg_read, .reg_write = sci_reg_write \
}

static void
lux2100_switch_bank(struct fpga *fpga, unsigned int val)
{
    const struct regdef r_datapath = REG_LUX2100(LUX2100_SCI_DP_SCIP_EN, "dp_scip_en");
    sci_reg_write(&r_datapath, fpga, val);
}

/*-------------------------------------
 * LUX2100/LUX8M SCI Sensor Registers
 *-------------------------------------
 */
static const struct regdef lux2100_sensor_regdefs[] = {
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

/* Setup action before we can access the sensor group registers */
static void
lux2100_sensor_setup(const struct reggroup *group, struct fpga *fpga)
{
    lux2100_switch_bank(fpga, 0);
}

const struct reggroup lux2100_sensor_registers = {
    .name = "LUX2100",
    .setup = lux2100_sensor_setup,
    .rtab = lux2100_sensor_regdefs,
};

/*-------------------------------------
 * LUX2100/LUX8M SCI Datapath Registers
 *-------------------------------------
 */
static const struct regdef lux2100_datapath_regdefs[] = {
    REG_LUX2100(LUX2100_SCI_DP_ID,              "dp_id"),
    REG_LUX2100(LUX2100_SCI_BLOCK_ID,           "block_id"),
    REG_LUX2100(LUX2100_SCI_CRC_EN,             "crc_en"),
    REG_LUX2100(LUX2100_SCI_GAIN_ENABLE,        "gain_enable"),
    REG_LUX2100(LUX2100_SCI_ODD_EVEN_SEL,       "odd_even_sel"),
    REG_LUX2100(LUX2100_SCI_ODD_EVEN_OS_EN,     "odd_even_os_en"),
    REG_LUX2100(LUX2100_SCI_NB_BIT_SEL,         "nb_bit_sel"),
    REG_LUX2100(LUX2100_SCI_MSB_FIRST_DATA,     "msb_first_data"),
    REG_LUX2100(LUX2100_SCI_CUST_DIG_PAT,       "cist_dig_pat"),
    REG_LUX2100(LUX2100_SCI_DIG_PAT_SEL,        "dig_pat_sel"),
    REG_LUX2100(LUX2100_SCI_SEL_RDOUT_DLY,      "sel_rdout_dly"),
    REG_LUX2100(LUX2100_SCI_LATCH_DLY,          "latch_dly"),
    REG_LUX2100(LUX2100_SCI_CAL_START,          "cal_start"),
    REG_LUX2100(LUX2100_SCI_RECAL_START,        "recal_start"),
    REG_LUX2100(LUX2100_SCI_NB_BITS_SAMPLES_AVG, "nb_bits_samples_avg"),
    REG_LUX2100(LUX2100_SCI_NB_BITS_OS_CAL_ITER, "nb_bits_os_cal_iter"),
    REG_LUX2100(LUX2100_SCI_ADC_OS_SEQ_WIDTH,    "adc_os_seq_width"),
    REG_LUX2100(LUX2100_SCI_OS_TARGET,           "os_target"),
    REG_LUX2100(LUX2100_SCI_ADC_OS_EN,           "adc_os_en"),
    REG_LUX2100(LUX2100_SCI_ADC_OS_SIGN,         "adc_os_sign"),
    /* ADC Offset registers. */
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x00),           "adc_os_0"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x01),           "adc_os_1"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x02),           "adc_os_2"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x03),           "adc_os_3"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x04),           "adc_os_4"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x05),           "adc_os_5"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x06),           "adc_os_6"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x07),           "adc_os_7"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x08),           "adc_os_8"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x09),           "adc_os_9"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x0A),           "adc_os_a"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x0B),           "adc_os_b"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x0C),           "adc_os_c"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x0D),           "adc_os_d"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x0E),           "adc_os_e"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x0F),           "adc_os_f"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x10),           "adc_os_10"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x11),           "adc_os_11"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x12),           "adc_os_12"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x13),           "adc_os_13"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x14),           "adc_os_14"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x15),           "adc_os_15"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x16),           "adc_os_16"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x17),           "adc_os_17"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x18),           "adc_os_18"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x19),           "adc_os_19"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x1A),           "adc_os_1a"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x1B),           "adc_os_1b"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x1C),           "adc_os_1c"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x1D),           "adc_os_1d"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x1E),           "adc_os_1e"),
    REG_LUX2100(LUX2100_SCI_ADC_OS(0x1F),           "adc_os_1f"),
    {NULL, 0, 0, 0}
};

/* Setup action before we can access the sensor group registers */
static void
lux2100_datapath_setup(const struct reggroup *group, struct fpga *fpga)
{
    lux2100_switch_bank(fpga, 1);
}
static void
lux2100_datapath_cleanup(const struct reggroup *group, struct fpga *fpga)
{
    lux2100_switch_bank(fpga, 0);
}

const struct reggroup lux2100_datapath_registers = {
    .name = "LUX2100 Datapath",
    .setup = lux2100_datapath_setup,
    .cleanup = lux2100_datapath_cleanup,
    .rtab = lux2100_datapath_regdefs,
};
