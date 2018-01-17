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
#ifndef _LUX1310_H
#define _LUX1310_H

#include <stdint.h>
#include <sys/types.h>

/* Pack an address and mask together into one word. */
#define LUX1310_SCI_REG_MASK            0xffff
#define LUX1310_SCI_REG_ADDR            16
#define LUX1310_SCI_REG(_addr_, _mask_)  (((_addr_) << LUX1310_SCI_REG_ADDR) | ((_mask_) & LUX1310_SCI_REG_MASK))

#define LUX1310_SCI_REV_CHIP            LUX1310_SCI_REG(0x00, 0x00ff)
#define LUX1310_SCI_CHIP_ID             LUX1310_SCI_REG(0x00, 0xff00)
#define LUX1310_SCI_TIMING_EN           LUX1310_SCI_REG(0x01, 0x0001)
#define LUX1310_SCI_SOF_DELAY           LUX1310_SCI_REG(0x02, 0x00ff)
#define LUX1310_SCI_HBLANK              LUX1310_SCI_REG(0x03, 0xffff)
#define LUX1310_SCI_ROI_NB              LUX1310_SCI_REG(0x04, 0x0007)
#define LUX1310_SCI_ROI_EN              LUX1310_SCI_REG(0x04, 0x0008)
#define LUX1310_SCI_DRK_COL_RD          LUX1310_SCI_REG(0x04, 0x0100)
#define LUX1310_SCI_VFLIP               LUX1310_SCI_REG(0x04, 0x1000)
#define LUX1310_SCI_HFLIP               LUX1310_SCI_REG(0x04, 0x2000)
#define LUX1310_SCI_X_START             LUX1310_SCI_REG(0x05, 0x07ff)
#define LUX1310_SCI_X_END               LUX1310_SCI_REG(0x06, 0x07ff)
#define LUX1310_SCI_Y_START             LUX1310_SCI_REG(0x07, 0x07ff)
#define LUX1310_SCI_Y_END               LUX1310_SCI_REG(0x08, 0x07ff)
#define LUX1310_SCI_ROI_X_START(_n_)    LUX1310_SCI_REG(0x09 + 4 * (_n_), 0x07ff)
#define LUX1310_SCI_ROI_X_END(_n_)      LUX1310_SCI_REG(0x10 + 4 * (_n_), 0x07ff)
#define LUX1310_SCI_ROI_Y_START(_n_)    LUX1310_SCI_REG(0x11 + 4 * (_n_), 0x07ff)
#define LUX1310_SCI_ROI_Y_END(_n_)      LUX1310_SCI_REG(0x12 + 4 * (_n_), 0x07ff)
#define LUX1310_SCI_DRK_ROWS_ST_ADDR    LUX1310_SCI_REG(0x29, 0x07ff)
#define LUX1310_SCI_NB_DRK_ROWS         LUX1310_SCI_REG(0x29, 0xf000)
#define LUX1310_SCI_NEXT_ROW_ADDR_OVR   LUX1310_SCI_REG(0x2A, 0x07ff)
#define LUX1310_SCI_NEXT_ROW_OVR_EN     LUX1310_SCI_REG(0x2A, 0x1000)
#define LUX1310_SCI_INTER_ROI_SP        LUX1310_SCI_REG(0x2B, 0x0001)
#define LUX1310_SCI_CLK_SEL_SCIP        LUX1310_SCI_REG(0x2C, 0x0003)
#define LUX1310_SCI_CLK_SEL_FT          LUX1310_SCI_REG(0x2C, 0x000c)
#define LUX1310_SCI_FT_TRIG_NB_PULSE    LUX1310_SCI_REG(0x31, 0xffff)
#define LUX1310_SCI_FT_RST_NB_PULSE     LUX1310_SCI_REG(0x33, 0xffff)
#define LUX1310_SCI_ABN2_EN             LUX1310_SCI_REG(0x35, 0x0010)
#define LUX1310_SCI_RDOUT_DLY           LUX1310_SCI_REG(0x37, 0xffff)
#define LUX1310_SCI_ADC_CAL_EN          LUX1310_SCI_REG(0x39, 0x0001)
#define LUX1310_SCI_ADC_OS(_x_)         LUX1310_SCI_REG(0x3A + (_x_), 0x07ff)
#define LUX1310_SCI_ADC_OS_SEQ_WIDTH    LUX1310_SCI_REG(0x4A, 0x03ff)
#define LUX1310_SCI_PCLK_LINEVALID      LUX1310_SCI_REG(0x4B, 0x0fff)
#define LUX1310_SCI_PCLK_VBLANK         LUX1310_SCI_REG(0x4C, 0x0fff)
#define LUX1310_SCI_PCLK_HBLANK         LUX1310_SCI_REG(0x4D, 0x0fff)
#define LUX1310_SCI_PCLK_OPTICAL_BLACK  LUX1310_SCI_REG(0x4E, 0x0fff)
#define LUX1310_SCI_MONO                LUX1310_SCI_REG(0x4F, 0x0001)
#define LUX1310_SCI_ROW2EN              LUX1310_SCI_REG(0x4F, 0x0010)
#define LUX1310_SCI_POUTSEL             LUX1310_SCI_REG(0x60, 0x0003)
#define LUX1310_SCI_INVERT_ANALOG       LUX1310_SCI_REG(0x50, 0x0010)
#define LUX1310_SCI_GLOBAL_SHUTTER      LUX1310_SCI_REG(0x50, 0x0100)
#define LUX1310_SCI_GAIN_SEL_SAMP       LUX1310_SCI_REG(0x51, 0x0fff)
#define LUX1310_SCI_GAIN_SEL_FB         LUX1310_SCI_REG(0x52, 0x007f)
#define LUX1310_SCI_GAIN_BIT            LUX1310_SCI_REG(0x53, 0x0007)
#define LUX1310_SCI_COLBIN2             LUX1310_SCI_REG(0x55, 0x0001)
#define LUX1310_SCI_TST_PAT             LUX1310_SCI_REG(0x56, 0x0003)
#define LUX1310_SCI_CUST_PAT            LUX1310_SCI_REG(0x57, 0x0fff)
#define LUX1310_SCI_MUX_MODE            LUX1310_SCI_REG(0x58, 0x0003)
#define LUX1310_SCI_PWR_EN_SERIALIZER_B LUX1310_SCI_REG(0x59, 0xffff)
#define LUX1310_SCI_DAC_ILV             LUX1310_SCI_REG(0x5A, 0x0007)
#define LUX1310_SCI_MSB_FIRST_DATA      LUX1310_SCI_REG(0x5A, 0x0008)
#define LUX1310_SCI_PCLK_INV            LUX1310_SCI_REG(0x5A, 0x0010)
#define LUX1310_SCI_TERMB_DATA          LUX1310_SCI_REG(0x5A, 0x0020)
#define LUX1310_SCI_DCLK_INV            LUX1310_SCI_REG(0x5A, 0x0040)
#define LUX1310_SCI_TERMB_CLK           LUX1310_SCI_REG(0x5A, 0x0080)
#define LUX1310_SCI_TERMB_RXCLK         LUX1310_SCI_REG(0x5B, 0x1000)
#define LUX1310_SCI_PWREN_DCLK_B        LUX1310_SCI_REG(0x60, 0x0001)
#define LUX1310_SCI_PWREN_PCLK_B        LUX1310_SCI_REG(0x60, 0x0002)
#define LUX1310_SCI_PWREN_BIAS_B        LUX1310_SCI_REG(0x60, 0x0004)
#define LUX1310_SCI_PWREN_DRV_B         LUX1310_SCI_REG(0x60, 0x0008)
#define LUX1310_SCI_PWREN_ADC_B         LUX1310_SCI_REG(0x60, 0x0010)
#define LUX1310_SCI_SEL_VCMI            LUX1310_SCI_REG(0x62, 0x000f)
#define LUX1310_SCI_SEL_VCMO            LUX1310_SCI_REG(0x62, 0x00f0)
#define LUX1310_SCI_SEL_VCMP            LUX1310_SCI_REG(0x62, 0x0f00)
#define LUX1310_SCI_SEL_VCMN            LUX1310_SCI_REG(0x62, 0xf000)
#define LUX1310_SCI_HIDY_EN             LUX1310_SCI_REG(0x67, 0x0001)
#define LUX1310_SCI_HIDY_TRIG_NB_PULSE  LUX1310_SCI_REG(0x68, 0xffff)
#define LUX1310_SCI_SEL_VDR1_WIDTH      LUX1310_SCI_REG(0x69, 0xffff)
#define LUX1310_SCI_SEL_VDR2_WIDTH      LUX1310_SCI_REG(0x6A, 0xffff)
#define LUX1310_SCI_SEL_VDR3_WIDTH      LUX1310_SCI_REG(0x6B, 0xffff)
#define LUX1310_SCI_SER_SYNC            LUX1310_SCI_REG(0x7c, 0x0001)
#define LUX1310_SCI_CLK_SYNC            LUX1310_SCI_REG(0x7d, 0x0001)
#define LUX1310_SCI_SRESET_B            LUX1310_SCI_REG(0x7e, 0x0001)

/* Undocumented Registers */
#define LUX1310_SCI_STATE_IDLE_CTRL0    LUX1310_SCI_REG(0x2d, 0xffff)
#define LUX1310_SCI_STATE_IDLE_CTRL1    LUX1310_SCI_REG(0x2e, 0xffff)
#define LUX1310_SCI_STATE_IDLE_CTRL2    LUX1310_SCI_REG(0x2f, 0xffff)
#define LUX1310_SCI_ADC_CLOCK_CTRL      LUX1310_SCI_REG(0x5c, 0xffff)
#define LUX1310_SCI_LINE_VALID_DLY      LUX1310_SCI_REG(0x71, 0xffff)
#define LUX1310_SCI_INT_CLK_TIMING      LUX1310_SCI_REG(0x74, 0xffff)
#define LUX1310_SCI_WAVETAB_SIZE        LUX1310_SCI_REG(0x7a, 0xffff)

#define LUX1310_SCI_REGISTER_COUNT  0x7f

/* TODO: Add a function to generate this dynamically */
struct lux1310_wavetab {
    unsigned int read_delay;
    unsigned int start_delay;
    const uint8_t *table;
    size_t len;
};

/* Some built-in wavetables. */
const struct lux1310_wavetab lux1310_wt_sram80;
const struct lux1310_wavetab lux1310_wt_sram39_blk;
const struct lux1310_wavetab lux1310_wt_sram39;
const struct lux1310_wavetab lux1310_wt_sram30;
const struct lux1310_wavetab lux1310_wt_sram25;
const struct lux1310_wavetab lux1310_wt_sram20;

#endif /* _LUX1310_H */
