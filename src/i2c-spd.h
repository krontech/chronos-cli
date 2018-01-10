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
#ifndef _I2C_SPD_H
#define _I2C_SPD_H

#include <stdint.h>
#include <cli-utils.h>

#define SPD_I2C_ADDR(_x_)       (0x50 + (_x_))

#define SPD_VERSION_MAJOR       (0xf << 4)
#define SPD_VERSION_MINOR       (0xf << 0)
#define SPD_MEMORY_TYPE_SDRAM   4
#define SPD_MEMORY_TYPE_DDR     7
#define SPD_MEMORY_TYPE_DDR2    8
#define SPD_MEMORY_TYPE_DDR3    11

#define SPD_MODULE_TYPE         (0xf << 0)
#define SPD_MODULE_TYPE_UDIMM   (2 << 0)
#define SPD_MODULE_TYPE_SODIMM  (3 << 0)
#define SPD_MODULE_TYPE_LRDIMM  (11 << 0)

#define SPD_VOLTAGE_1V25        (1<<2)
#define SPD_VOLTAGE_1V35        (1<<1)
#define SPD_VOLTAGE_NOT_1V50    (1<<0)

#define SPD_BANK_ADDR_BITS(_reg_)   ((((_reg_) >> 4) & 0x7) + 3)
#define SPD_BITS_PER_CHIP(_reg_)    ((((_reg_) >> 0) & 0xf) + 28)
#define SPD_ROW_ADDR_BITS(_reg_)    ((((_reg_) >> 3) & 0x7) + 12)
#define SPD_COL_ADDR_BITS(_reg_)    ((((_reg_) >> 0) & 0x7) + 9)
#define SPD_RANKS(_reg_)            ((((_reg_) >> 3) & 0x7) + 1)
#define SPD_IO_BITS_PER_CHIP(_reg_) ((((_reg_) >> 0) & 0x7) + 2)
#define SPD_ECC_BITS                (0x7 << 3)
#define SPD_DATA_BITS(_reg_)        ((((_reg_) >> 0) & 0x7) + 3)

#define SPD_FTB_DIVIDEND(_reg_)     (((_reg_) >> 4) & 0xf)
#define SPD_FTB_DIVISOR(_reg_)      (((_reg_) >> 0) & 0xf)

/* JEDEC SPD Information. */
struct ddr3_spd {
    uint8_t nbytes;
    uint8_t version;
    uint8_t type;
    uint8_t module;
    uint8_t banks;
    uint8_t rowcol;
    uint8_t voltage;
    uint8_t ranks;
    uint8_t ecc;
    uint8_t ftb;
    uint8_t mtb_divisor;
    uint8_t mtb_dividend;
    uint8_t t_ck_min;
    uint8_t __reserved0[1];
    uint8_t cas_support[2];
    uint8_t t_aa_min;
    uint8_t t_wr_min;
    uint8_t t_rcd_min;
    uint8_t t_rrd_min;
    uint8_t t_rp_min;
    uint8_t hi_rc_ras;
    uint8_t t_ras_min;
    uint8_t t_rc_min;
    uint8_t t_rfc_min[2];
    uint8_t t_wtr_min;
    uint8_t t_rtp_min;
    uint8_t t_faw_min[2];
    uint8_t sdram_features;
    uint8_t sdram_refresh;
    uint8_t dimm_sensor;
    uint8_t nonstd;
    int8_t  t_ck_corr;
    int8_t  t_aa_corr;
    int8_t  t_rcd_corr;
    int8_t  t_rp_corr;
    int8_t  t_rc_corr;
    uint8_t __reserved1[21];
    uint8_t height;
    uint8_t thickness;
    uint8_t ref_design;
    uint8_t module_data[54];
    uint8_t mfr_id[2];
    uint8_t mfr_location;
    uint8_t mfr_year;
    uint8_t mfr_week;
    uint8_t serial[4];
    uint8_t crc[2];
};

extern const struct enumval spd_ram_types[];
extern const struct enumval spd_mod_types[];

const char *spd_size_readable(const struct ddr3_spd *spd, char *dst, size_t maxlen);
unsigned long long spd_size_bytes(const struct ddr3_spd *spd);
void spd_fprint(const struct ddr3_spd *spd, FILE *stream);
void spd_fprint_timing(const struct ddr3_spd *spd, FILE *stream);

#endif /* _I2C_SPD_H */
