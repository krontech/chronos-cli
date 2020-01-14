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
#include "lux1310.h"

/*-------------------------------------
 * LUX1310 SCI Registers
 *-------------------------------------
 */
#define REG_LUX1310(_reg_, _name_) { \
    .name = _name_, .offset = (_reg_) >> LUX1310_SCI_REG_ADDR, \
    .mask = (_reg_) & LUX1310_SCI_REG_MASK, .size = 2, \
    .reg_read = sci_reg_read \
}

static int
lux1310_detect(const struct reggroup *group, struct fpga *fpga)
{
    return (sci_read_chipid(fpga) == 0xDA);
}

static const struct regdef lux1310_regdefs[] = {
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
const struct reggroup lux1310_registers = {
    .name = "LUX1310",
    .filter = "lux1310",
    .rtab = lux1310_regdefs,
};
