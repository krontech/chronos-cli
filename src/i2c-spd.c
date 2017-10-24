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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include "cli-utils.h"
#include "i2c-spd.h"

const struct enumval spd_ram_types[] = {
    {.value = SPD_MEMORY_TYPE_SDRAM,    .name = "SDRAM" },
    {.value = SPD_MEMORY_TYPE_DDR,      .name = "DDR SDRAM" },
    {.value = SPD_MEMORY_TYPE_DDR2,     .name = "DDR2" },
    {.value = SPD_MEMORY_TYPE_DDR3,     .name = "DDR3" },
    { 0, 0 },
};
const struct enumval spd_mod_types[] = {
    {.value = SPD_MODULE_TYPE_UDIMM,    .name = "UDIMM" },
    {.value = SPD_MODULE_TYPE_SODIMM,   .name = "SODIMM" },
    {.value = SPD_MODULE_TYPE_LRDIMM,   .name = "LRDIMM" },
    { 0, 0 },
};

static const char *
bitsize_prefix(int bits)
{
    switch (bits / 10) {
        case 0:
            return "";
        case 1:
            return "ki";
        case 2:
            return "Mi";
        case 3:
            return "Gi";
        case 4:
            return "Ti";
        default:
            return "OMGi";
    } /* switch */
}

void
spd_fprint(const struct ddr3_spd *spd, FILE *stream)
{
    int bits;

    /* Module Information */
    fprintf(stream, "\tSPD Version: %lu.%lu\n", getbits(spd->version, SPD_VERSION_MAJOR), getbits(spd->version, SPD_VERSION_MINOR));
    fprintf(stream, "\tMemory Type: %s (0x%02x)\n", enumval_name(spd_ram_types, spd->type, "Unknown"), spd->type);
    fprintf(stream, "\tModule Type: %s (0x%02x)\n", enumval_name(spd_mod_types, spd->module, "Unknown"), spd->module);
    fprintf(stream, "\tOperating Voltage:");
    if (spd->voltage & SPD_VOLTAGE_1V25) {
        fprintf(stream, " 1.25V");
    }
    if (spd->voltage & SPD_VOLTAGE_1V35) {
        fprintf(stream, " 1.35V");
    }
    if (!(spd->voltage & SPD_VOLTAGE_NOT_1V50)) {
        fprintf(stream, " 1.50V");
    }
    fprintf(stream, "\n");

    /* Memory Addressing and Layout */
    bits = SPD_BITS_PER_CHIP(spd->banks);
    fprintf(stream, "\tMemory Chip Size: %d %sb\n", 1 << (bits % 10), bitsize_prefix(bits));
    fprintf(stream, "\tAddress Bits: %d bank, %d row, %d colum\n",
        SPD_BANK_ADDR_BITS(spd->banks), SPD_ROW_ADDR_BITS(spd->rowcol), SPD_COL_ADDR_BITS(spd->rowcol));
    fprintf(stream, "\tRanks: %d\n", SPD_RANKS(spd->ranks));

    bits = 1 << SPD_DATA_BITS(spd->ecc);
    fprintf(stream, "\tMemory Width: %d bits%s (%dx%d bits)\n", bits, ((spd->ecc & SPD_ECC_BITS) ? " with ECC" : ""),
        bits >> SPD_IO_BITS_PER_CHIP(spd->ranks), 1 << SPD_IO_BITS_PER_CHIP(spd->ranks));

    /* Manufacturing Data */
    /* TODO: Add a verbose flag and put this back in along with the timing data. */
    //fprintf(stream, "\tManufacture ID: %02x%02x\n", spd->mfr_id[1], spd->mfr_id[0]);
    //fprintf(strema, "\tManufacture Location: 0x%02x\n", spd->mfr_location);
    fprintf(stream, "\tManufacture Date: %02x%02x\n", spd->mfr_year, spd->mfr_week);
    fprintf(stream, "\tSerial Number: %02x:%02x:%02x:%02x\n", spd->serial[0], spd->serial[1], spd->serial[2], spd->serial[3]);
} /* spd_fprint */


static void
spd_fprint_mtb(const struct ddr3_spd *spd, FILE *stream, const char *name, unsigned long mtb, int8_t ftb)
{
    unsigned long picosec = (1000UL * mtb * spd->mtb_dividend) / spd->mtb_divisor;
    if (ftb) {
        picosec += (ftb * SPD_FTB_DIVIDEND(spd->ftb)) / SPD_FTB_DIVISOR(spd->ftb);
    }
    fprintf(stream, "\t%s: %lu ps (%lu mtb)\n", name, picosec, mtb);
}

void
spd_fprint_timing(const struct ddr3_spd *spd, FILE *stream)
{
    fprintf(stream, "\tMedium Time Base: %d/%d ns\n", spd->mtb_dividend, spd->mtb_divisor);
    fprintf(stream, "\tFine Time Base: %d/%d ps\n", SPD_FTB_DIVIDEND(spd->ftb), SPD_FTB_DIVISOR(spd->ftb));
    spd_fprint_mtb(spd, stream, "t_ck_min", spd->t_ck_min, 0);
    spd_fprint_mtb(spd, stream, "t_aa_min", spd->t_aa_min, spd->t_aa_corr);
    spd_fprint_mtb(spd, stream, "t_wr_min", spd->t_wr_min, 0);
    spd_fprint_mtb(spd, stream, "t_rcd_min", spd->t_rcd_min, spd->t_rcd_corr);
    spd_fprint_mtb(spd, stream, "t_rrd_min", spd->t_rrd_min, 0);
    spd_fprint_mtb(spd, stream, "t_rp_min", spd->t_rp_min, spd->t_rp_corr);
    /* TODO: t_rc, t_ras and t_rfc are multibyte values. */
    spd_fprint_mtb(spd, stream, "t_wtr_min", spd->t_wtr_min, 0);
    spd_fprint_mtb(spd, stream, "t_rtp_min", spd->t_rtp_min, 0);
    /* TODO: t_faw is a multibyte value. */
} /* spd_fprint_timing */

/* Return the slot size as a human readable string, or NULL if unsupported. */
const char *
spd_size_readable(const struct ddr3_spd *spd, char *dst, size_t maxlen)
{
    if (spd->type != SPD_MEMORY_TYPE_DDR3) {
        return NULL;
    }
    else {
        unsigned long long wordsz = (1 << SPD_DATA_BITS(spd->ecc)) / 8;
        unsigned long bits = SPD_BANK_ADDR_BITS(spd->banks) + SPD_ROW_ADDR_BITS(spd->rowcol) + SPD_COL_ADDR_BITS(spd->rowcol);
        unsigned long long sz = (wordsz << bits) * SPD_RANKS(spd->ranks);
        int i = 0;
        while ((sz >> i) > 1024) {
            i += 10;
        }
        snprintf(dst, maxlen, "%llu %sB", sz >> i, bitsize_prefix(i));
        return dst;
    }
} /* spd_size_readable */
