
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cli.h"
#include "fpga.h"
#include "fpga-lux1310.h"

struct lux1310_regmap {
    uint32_t reg;
    const char *name;
};

static const struct lux1310_regmap lux1310_map[] = {
    {LUX1310_SCI_REV_CHIP,      "rev_chip"},
    {LUX1310_SCI_CHIP_ID,       "reg_chip_id"},
    {LUX1310_SCI_TIMING_EN,     "reg_timing_en"},
    {LUX1310_SCI_SOF_DELAY,     "reg_sof_delay"},
    {LUX1310_SCI_HBLANK,        "reg_hblank"},
    {LUX1310_SCI_ROI_NB,        "reg_roi_nb"},
    {LUX1310_SCI_ROI_EN,        "reg_roi_en"},
    {LUX1310_SCI_DRK_COL_RD,    "reg_drk_col_rd",},
    {LUX1310_SCI_VFLIP,         "reg_vflip"},
    {LUX1310_SCI_HFLIP,         "reg_hflip"},
    {LUX1310_SCI_X_START,       "reg_x_start"},
    {LUX1310_SCI_X_END,         "reg_x_end"},
    {LUX1310_SCI_Y_START,       "reg_y_start"},
    {LUX1310_SCI_Y_END,         "reg_y_end"},
    /* TODO: ROI Y/X geometry */
    {LUX1310_SCI_DRK_ROWS_ST_ADDR,  "reg_drk_rows_st_addr"},
    {LUX1310_SCI_NB_DRK_ROWS,       "reg_nb_drk_rows"},
    {LUX1310_SCI_NEXT_ROW_ADDR_OVR, "reg_next_row_addr_ovr"},
    {LUX1310_SCI_NEXT_ROW_OVR_EN,   "reg_next_row_ovr_en"},
    {LUX1310_SCI_INTER_ROI_SP,      "reg_inter_roi_sp"},
    {LUX1310_SCI_CLK_SEL_SCIP,      "reg_clk_sel_scip"},
    {LUX1310_SCI_CLK_SEL_FT,        "reg_clk_sel_ft"},
    {LUX1310_SCI_FT_TRIG_NB_PULSE,  "reg_ft_trig_nb_pulse"},
    {LUX1310_SCI_FT_RST_NB_PULSE,   "reg_ft_rst_nb_pulse"},
    {LUX1310_SCI_ABN2_EN,           "reg_abn2_en"},
    {LUX1310_SCI_RDOUT_DLY,         "reg_rdout_dly"},
    {LUX1310_SCI_ADC_CAL_EN,        "reg_adc_cal_en"},
    /* TODO: ADC Offset registers. */
    {LUX1310_SCI_ADC_OS_SEQ_WIDTH,  "reg_adc_os_seq_width"},
    {LUX1310_SCI_PCLK_LINEVALID,    "reg_pclk_linevalid"},
    {LUX1310_SCI_PCLK_VBLANK,       "reg_pclk_vblank"},
    {LUX1310_SCI_PCLK_HBLANK,       "reg_pclk_hblank"},
    {LUX1310_SCI_PCLK_OPTICAL_BLACK,"reg_pclk_optical_black"},
    {LUX1310_SCI_MONO,              "reg_mono"},
    {LUX1310_SCI_ROW2EN,            "reg_row2en"},
    {LUX1310_SCI_POUTSEL,           "reg_poutsel"},
    {LUX1310_SCI_INVERT_ANALOG,     "reg_invert_analog"},
    {LUX1310_SCI_GLOBAL_SHUTTER,    "reg_global_shutter"},
    {LUX1310_SCI_GAIN_SEL_SAMP,     "reg_gain_sel_samp"},
    {LUX1310_SCI_GAIN_SEL_FB,       "reg_gain_sel_fb"},
    {LUX1310_SCI_GAIN_BIT,          "reg_gain_bit"},
    {LUX1310_SCI_COLBIN2,           "reg_colbin2"},
    {LUX1310_SCI_TST_PAT,           "reg_tst_pat"},
    {LUX1310_SCI_CUST_PAT,          "reg_cust_pat"},
    {LUX1310_SCI_MUX_MODE,          "reg_mux_mode"},
    {LUX1310_SCI_PWR_EN_SERIALIZER_B, "reg_pwr_en_serializer_b"},
    {LUX1310_SCI_DAC_ILV,           "reg_dac_ilv"},
    {LUX1310_SCI_MSB_FIRST_DATA,    "reg_msb_first_data"},
    {LUX1310_SCI_PCLK_INV,          "reg_pclk_inv"},
    {LUX1310_SCI_TERMB_DATA,        "reg_termb_data"},
    {LUX1310_SCI_DCLK_INV,          "reg_dclk_inv"},
    {LUX1310_SCI_TERMB_CLK,         "reg_termb_clk"},
    {LUX1310_SCI_TERMB_RXCLK,       "reg_termb_rxclk"},
    {LUX1310_SCI_PWREN_DCLK_B,      "reg_pwren_dclk_b"},
    {LUX1310_SCI_PWREN_PCLK_B,      "reg_pwren_pclk_b"},
    {LUX1310_SCI_PWREN_BIAS_B,      "reg_pwren_bias_b"},
    {LUX1310_SCI_PWREN_DRV_B,       "reg_pwren_drv_b"},
    {LUX1310_SCI_PWREN_ADC_B,       "reg_pwren_adc_b"},
    {LUX1310_SCI_SEL_VCMI,          "reg_sel_vcmi"},
    {LUX1310_SCI_SEL_VCMO,          "reg_sel_vcmo"},
    {LUX1310_SCI_SEL_VCMP,          "reg_sel_vcmp"},
    {LUX1310_SCI_SEL_VCMN,          "reg_sel_vcmn"},
    {LUX1310_SCI_HIDY_EN,           "reg_hidy_en"},
    {LUX1310_SCI_HIDY_TRIG_NB_PULSE,"reg_hidy_trig_nb_pulse"},
    {LUX1310_SCI_SEL_VDR1_WIDTH,    "reg_vdr1_width"},
    {LUX1310_SCI_SEL_VDR2_WIDTH,    "reg_vdr2_width"},
    {LUX1310_SCI_SEL_VDR3_WIDTH,    "reg_vdr3_width"},
    {LUX1310_SCI_SER_SYNC,          "reg_ser_sync"},
    {LUX1310_SCI_CLK_SYNC,          "reg_clk_sync"},
    {LUX1310_SCI_SRESET_B,          "reg_sreset_b"},

    /* List termination */
    {0, 0}
};

static int
getbits(uint16_t value, uint16_t mask)
{
    if (!mask) {
        return 0;
    }
    else {
        uint16_t lsb = (~mask + 1) & mask;
        return (value & mask) / lsb;
    }
}

static int
do_lux1310(struct fpga *fpga, char *const argv[], int argc)
{
    const char *header = "\t%-6s %-6s %-6s %24s  %s\n";
    const char *format = "\t0x%02x   0x%04x 0x%04x %24s  0x%x\n";
    uint16_t regs[LUX1310_SCI_REGISTER_COUNT];
    int i;

    /* Read the contents of the SCI memory. */
    for (i = 0; i < LUX1310_SCI_REGISTER_COUNT; i++) {
        regs[i] = fpga_sci_read(fpga, i);
    } /* for */

    /* Print the register mapping. */
    printf(header, "ADDR", "READ", "MASK", "NAME", "VALUE");
    for (i = 0; lux1310_map[i].name; i++) {
        const struct lux1310_regmap *r = &lux1310_map[i];
        uint8_t addr = (r->reg >> LUX1310_SCI_REG_ADDR);
        uint16_t mask = (r->reg & LUX1310_SCI_REG_MASK);
        printf(format, addr, regs[addr], mask, r->name, getbits(regs[addr], mask));
    } /* for */
} /* do_lux1310 */

/* The lux1310 subcommand */
const struct cli_subcmd cli_cmd_lux1310 = {
    .name = "lux1310",
    .desc = "Interract with the LUX1310 image sensor.",
    .function = do_lux1310,
};
