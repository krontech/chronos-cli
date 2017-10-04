/* Copyright 2017 Kron Technologies. All Rights Reserved. */
#ifndef _FPGA_LUX1310_H
#define _FPGA_LUX1310_H

#include <stdint.h>

#define LUX1310_SCI_REV_CHIP        0x00
#define LUX1310_SCI_TIMING_EN       0x01
#define LUX1310_SCI_SOF_DELAY       0x02
#define LUX1310_SCI_HBLANK          0x03
/* ??? misc junk at 0x04 */
#define LUX1310_SCI_X_START         0x05
#define LUX1310_SCI_X_END           0x06
#define LUX1310_SCI_Y_START         0x07
#define LUX1310_SCI_Y_END           0x08
#define LUX1310_SCI_ROI_X_START(_n_)    (0x09 + 4 * (_n_))
#define LUX1310_SCI_ROI_X_END(_n_)      (0x10 + 4 * (_n_))
#define LUX1310_SCI_ROI_Y_START(_n_)    (0x11 + 4 * (_n_))
#define LUX1310_SCI_ROI_Y_END(_n_)      (0x12 + 4 * (_n_))
#define LUX1310_SCI_DRK_ROWS        0x29
#define LUX1310_SCI_NEXT_ROWS       0x2A
#define LUX1310_SCI_INTER_ROI_SP    0x2B
#define LUX1310_SCI_CLK_SEL         0x2C
#define LUX1310_SCI_FT_TRIG_NB_PULSE 0x31
#define LUX1310_SCI_FT_RST_NB_PULSE 0x33
#define LUX1310_SCI_ABN2_EN         0x35

#endif /* _FPGA_LUX1310_H */
