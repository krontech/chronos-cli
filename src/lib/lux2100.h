/****************************************************************************
 *  Copyright (C) 2019 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#ifndef _LUX2100_H
#define _LUX2100_H

/* Pack an address and mask together into one word. */
#define LUX2100_SCI_REG_MASK            0xffff
#define LUX2100_SCI_REG_ADDR            16
#define LUX2100_SCI_REG(_addr_, _mask_)  (((_addr_) << LUX2100_SCI_REG_ADDR) | ((_mask_) & LUX2100_SCI_REG_MASK))

/* Sensor Registers */
#define LUX2100_SCI_REV_CHIP            LUX2100_SCI_REG(0x00, 0x00ff)
#define LUX2100_SCI_CHIP_ID             LUX2100_SCI_REG(0x00, 0xff00)
#define LUX2100_SCI_TIMING_EN           LUX2100_SCI_REG(0x01, 0x0001)
#define LUX2100_SCI_GLOBAL_SHUTTER      LUX2100_SCI_REG(0x01, 0x0010)
#define LUX2100_SCI_FTR_EN              LUX2100_SCI_REG(0x01, 0x0100)
#define LUX2100_SCI_FT_RST_EN           LUX2100_SCI_REG(0x01, 0x1000)
#define LUX2100_SCI_ROI_SEL             LUX2100_SCI_REG(0x02, 0x00FF)
#define LUX2100_SCI_INTER_ROI_SP        LUX2100_SCI_REG(0x02, 0x0100)
#define LUX2100_SCI_DRK_COL_RD          LUX2100_SCI_REG(0x02, 0x0200)
#define LUX2100_SCI_VFLIP               LUX2100_SCI_REG(0x02, 0x0400)
#define LUX2100_SCI_HFLIP               LUX2100_SCI_REG(0x02, 0x0800)
#define LUX2100_SCI_CLK_SEL_FT          LUX2100_SCI_REG(0x02, 0x3000)
#define LUX2100_SCI_HBLANK              LUX2100_SCI_REG(0x03, 0xffff)
#define LUX2100_SCI_DP_SCIP_EN          LUX2100_SCI_REG(0x04, 0x0003)
#define LUX2100_SCI_SHADOW_REGS         LUX2100_SCI_REG(0x05, 0x0001)
#define LUX2100_SCI_X_START             LUX2100_SCI_REG(0x06, 0x0ffe)
#define LUX2100_SCI_X_END               LUX2100_SCI_REG(0x07, 0x0ffe)
#define LUX2100_SCI_Y_START             LUX2100_SCI_REG(0x08, 0x0ffe)
#define LUX2100_SCI_Y_END               LUX2100_SCI_REG(0x09, 0x0ffe)
#define LUX2100_SCI_ROI_X_START(_n_)    LUX2100_SCI_REG(0x0A + 4 * (_n_), 0x0ffe)
#define LUX2100_SCI_ROI_X_END(_n_)      LUX2100_SCI_REG(0x0B + 4 * (_n_), 0x0ffe)
#define LUX2100_SCI_ROI_Y_START(_n_)    LUX2100_SCI_REG(0x0C + 4 * (_n_), 0x0ffe)
#define LUX2100_SCI_ROI_Y_END(_n_)      LUX2100_SCI_REG(0x0D + 4 * (_n_), 0x0ffe)
#define LUX2100_SCI_ROI_EXP(_n_)        LUX2100_SCI_REG(0x0D + 4 * (_n_), 0x1000)
#define LUX2100_SCI_ROI_EXP_LD(_n_)     LUX2100_SCI_REG(0x0D + 4 * (_n_), 0x2000)
#define LUX2100_SCI_DRK_ROWS_ST_ADDR_TOP LUX2100_SCI_REG(0x2A, 0x0ffe)
#define LUX2100_SCI_NB_DRK_ROWS_TOP     LUX2100_SCI_REG(0x2B, 0x001e)
#define LUX2100_SCI_NEXT_ROW_ADDR_OVR   LUX2100_SCI_REG(0x2C, 0x0ffe)
#define LUX2100_SCI_NEXT_ROW_OVR_EN     LUX2100_SCI_REG(0x2C, 0x1000)
#define LUX2100_SCI_SOF_DELAY           LUX2100_SCI_REG(0x30, 0xff00)
#define LUX2100_SCI_FT_TRIG_NB_PULSE    LUX2100_SCI_REG(0x31, 0xffff)
#define LUX2100_SCI_FT_RST_NB_PULSE     LUX2100_SCI_REG(0x32, 0xffff)
#define LUX2100_SCI_RDOUT_DLY           LUX2100_SCI_REG(0x34, 0xffff)
#define LUX2100_SCI_ROW_TIME            LUX2100_SCI_REG(0x35, 0xffff)
#define LUX2100_SCI_ABN_SEL             LUX2100_SCI_REG(0x36, 0x0001)
#define LUX2100_SCI_ABN2_EN             LUX2100_SCI_REG(0x36, 0x0010)
#define LUX2100_SCI_ABN2_ALT_PAT        LUX2100_SCI_REG(0x36, 0x0100)
#define LUX2100_SCI_ABN2_LD             LUX2100_SCI_REG(0x36, 0x1000)
#define LUX2100_SCI_X_ORG               LUX2100_SCI_REG(0x40, 0x0fff)
#define LUX2100_SCI_Y_ORG               LUX2100_SCI_REG(0x41, 0x0fff)
#define LUX2100_SCI_PCLK_LINEVALID      LUX2100_SCI_REG(0x52, 0x0fff)
#define LUx2100_SCI_PCLK_VBLANK         LUX2100_SCI_REG(0x53, 0x0fff)
#define LUX2100_SCI_PCLK_HBLANK         LUX2100_SCI_REG(0x54, 0x0fff)
#define LUX2100_SCI_PCLK_OPTICAL_BLACK  LUX2100_SCI_REG(0x55, 0x0fff)
#define LUX2100_SCI_MONO                LUX2100_SCI_REG(0x56, 0x0001)
#define LUX2100_SCI_ROWBIN2             LUX2100_SCI_REG(0x56, 0x0010)
#define LUX2100_SCI_ROW2EN              LUX2100_SCI_REG(0x56, 0x0020)
#define LUX2100_SCI_COLBIN2             LUX2100_SCI_REG(0x56, 0x0100)
#define LUX2100_SCI_COLBIN4             LUX2100_SCI_REG(0x56, 0x0200)
#define LUX2100_SCI_POUTSEL             LUX2100_SCI_REG(0x56, 0x3000)
#define LUX2100_SCI_INVERT_ANALOG       LUX2100_SCI_REG(0x56, 0x8000)
#define LUX2100_SCI_GAIN_SEL_SAMP       LUX2100_SCI_REG(0x57, 0x0fff)
#define LUX2100_SCI_GAIN_SEL_FB         LUX2100_SCI_REG(0x58, 0x007f)
#define LUX2100_SCI_GAIN_SERIAL         LUX2100_SCI_REG(0x58, 0x0700)
#define LUX2100_SCI_LV_DELAY            LUX2100_SCI_REG(0x5B, 0x003f)
#define LUX2100_SCI_CUST_PAT            LUX2100_SCI_REG(0x5E, 0x0fff)
#define LUX2100_SCI_TST_PAT             LUX2100_SCI_REG(0x5E, 0x7000)
#define LUX2100_SCI_PWR_EN_SERIALIZER_B LUX2100_SCI_REG(0x5F, 0x000f)
#define LUX2100_SCI_MUX_MODE            LUX2100_SCI_REG(0x5F, 0x0030)
#define LUX2100_SCI_DAC_ILV             LUX2100_SCI_REG(0x60, 0x0007)
#define LUX2100_SCI_PCLK_INV            LUX2100_SCI_REG(0x60, 0x0010)
#define LUX2100_SCI_DCLK_INV            LUX2100_SCI_REG(0x60, 0x0020)
#define LUX2100_SCI_TERMB_DATA          LUX2100_SCI_REG(0x60, 0x0100)
#define LUX2100_SCI_TERMB_CLK           LUX2100_SCI_REG(0x60, 0x0200)
#define LUX2100_SCI_SEL_VLNKEEP_RST     LUX2100_SCI_REG(0x67, 0x1f00)
#define LUX2100_SCI_SEL_VDUM            LUX2100_SCI_REG(0x67, 0x6000)
#define LUX2100_SCI_SEL_VDUMRST         LUX2100_SCI_REG(0x69, 0x03e0)
#define LUX2100_SCI_SEL_VLNKEEP         LUX2100_SCI_REG(0x69, 0x7c00)
#define LUX2100_SCI_HIDY_EN             LUX2100_SCI_REG(0x6D, 0x0001)
#define LUX2100_SCI_GLB_FLUSH_EN        LUX2100_SCI_REG(0x6D, 0x0010)
#define LUX2100_SCI_SEL_VDR1_WIDTH      LUX2100_SCI_REG(0x6F, 0xffff)
#define LUX2100_SCI_SEL_VDR2_WIDTH      LUX2100_SCI_REG(0x70, 0xffff)
#define LUX2100_SCI_SEL_VDR3_WIDTH      LUX2100_SCI_REG(0x71, 0xffff)
#define LUX2100_SCI_ICOL_CAP_EN         LUX2100_SCI_REG(0x76, 0x00f0)
#define LUX2100_SCI_SRESET_B            LUX2100_SCI_REG(0x7e, 0x0001)
#define LUX2100_SCI_SER_SYNC            LUX2100_SCI_REG(0x7e, 0x0010)

/* Datapath Registers */
#define LUX2100_SCI_DP_ID               LUX2100_SCI_REG(0x00, 0x0001)
#define LUX2100_SCI_BLOCK_ID            LUX2100_SCI_REG(0x00, 0xfffe)
#define LUX2100_SCI_CRC_EN              LUX2100_SCI_REG(0x01, 0x0001)
#define LUX2100_SCI_GAIN_ENABLE         LUX2100_SCI_REG(0x01, 0x0010)
#define LUX2100_SCI_ODD_EVEN_SEL        LUX2100_SCI_REG(0x01, 0x0100)
#define LUX2100_SCI_ODD_EVEN_OS_EN      LUX2100_SCI_REG(0x01, 0x1000)
#define LUX2100_SCI_NB_BIT_SEL          LUX2100_SCI_REG(0x02, 0x0003)
#define LUX2100_SCI_MSB_FIRST_DATA      LUX2100_SCI_REG(0x02, 0x0010)
#define LUX2100_SCI_CUST_DIG_PAT        LUX2100_SCI_REG(0x03, 0x0fff)
#define LUX2100_SCI_DIG_PAT_SEL         LUX2100_SCI_REG(0x03, 0x3000)
#define LUX2100_SCI_SEL_RDOUT_DLY       LUX2100_SCI_REG(0x05, 0x007f)
#define LUX2100_SCI_LATCH_DLY           LUX2100_SCI_REG(0x06, 0x0001)
#define LUX2100_SCI_CAL_START           LUX2100_SCI_REG(0x0A, 0x0001)
#define LUX2100_SCI_RECAL_START         LUX2100_SCI_REG(0x0A, 0x0010)
#define LUX2100_SCI_NB_BITS_SAMPLES_AVG LUX2100_SCI_REG(0x0B, 0x000f)
#define LUX2100_SCI_NB_BITS_OS_CAL_ITER LUX2100_SCI_REG(0x0B, 0x00f0)
#define LUX2100_SCI_ADC_OS_SEQ_WIDTH    LUX2100_SCI_REG(0x0C, 0x003f)
#define LUX2100_SCI_OS_TARGET           LUX2100_SCI_REG(0x0D, 0x0fff)
#define LUX2100_SCI_ADC_OS_EN           LUX2100_SCI_REG(0x0E, 0x0001)
#define LUX2100_SCI_ADC_OS_SIGN         LUX2100_SCI_REG(0x0F, 0xffff)
#define LUX2100_SCI_ADC_OS(_n_)         LUX2100_SCI_REG(((_n_) < 16 ? 0x10 : 0x40) + (_n_), 0x03ff)
#define LUX2100_SCI_GAIN_SETVAL(_n_)    LUX2100_SCI_REG(((_n_) < 16 ? 0x20 : 0x50) + (_n_), 0xffff)
#define LUX2100_SCI_ODDEVEN_ROW_OS(_n_) LUX2100_SCI_REG(((_n_) < 16 ? 0x30 : 0x60) + (_n_), 0x03ff)

#endif /* _LUX2100_H */
