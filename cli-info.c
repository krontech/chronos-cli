#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include "cli.h"
#include "fpga.h"
#include "i2c.h"

struct regenum {
    const char *name;
    unsigned long value;
};

static const char *
regenum_name(const struct regenum *list, unsigned long value, const char *unknown)
{
    while (list->name) {
        if (list->value == value) {
            return list->name;
        }
        list++;
    }
    return unknown;
} /* regenum_name */

#define LUX1310_COLOR_DETECT        "/sys/class/gpio/gpio34/value"
#define EEPROM_I2C_BUS              "/dev/i2c-1"
#define EEPROM_I2C_ADDR_CAMERA      0x54
#define EEPROM_I2C_ADDR_SPD(_x_)    (0x50 + (_x_))

#define EEPROM_CAMERA_SERIAL_OFFSET 0
#define EEPROM_CAMERA_SERIAL_LEN    32

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

const struct regenum spd_types[] = {
    {.value = SPD_MEMORY_TYPE_SDRAM,    .name = "SDRAM" },
    {.value = SPD_MEMORY_TYPE_DDR,      .name = "DDR SDRAM" },
    {.value = SPD_MEMORY_TYPE_DDR2,     .name = "DDR2" },
    {.value = SPD_MEMORY_TYPE_DDR3,     .name = "DDR3" },
    { 0, 0 },
};
const struct regenum mod_types[] = {
    {.value = SPD_MODULE_TYPE_UDIMM,    .name = "UDIMM" },
    {.value = SPD_MODULE_TYPE_SODIMM,   .name = "SODIMM" },
    {.value = SPD_MODULE_TYPE_LRDIMM,   .name = "LRDIMM" },
    { 0, 0 },
};

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

static uint16_t
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

/* Return the slot size as a human readable string, or NULL if unsupported. */
static const char *
spd_size_readable(const struct ddr3_spd *spd, char *dst)
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
        sprintf(dst, "%llu %sB", sz >> i, bitsize_prefix(i));
        return dst;
    }
} /* spd_size_readable */

static void
print_spd(const struct ddr3_spd *spd)
{
    int bits;

    /* Module Information */
    printf("\tSPD Version: %d.%d\n", getbits(spd->version, SPD_VERSION_MAJOR), getbits(spd->version, SPD_VERSION_MINOR));
    printf("\tMemory Type: %s (0x%02x)\n", regenum_name(spd_types, spd->type, "Unknown"), spd->type);
    printf("\tModule Type: %s (0x%02x)\n", regenum_name(mod_types, spd->module, "Unknown"), spd->module);
    printf("\tOperating Voltage:");
    if (spd->voltage & SPD_VOLTAGE_1V25) {
        printf(" 1.25V");
    }
    if (spd->voltage & SPD_VOLTAGE_1V35) {
        printf(" 1.35V");
    }
    if (!(spd->voltage & SPD_VOLTAGE_NOT_1V50)) {
        printf(" 1.50V");
    }
    printf("\n");

    /* Memory Addressing and Layout */
    bits = SPD_BITS_PER_CHIP(spd->banks);
    printf("\tMemory Chip Size: %d %sb\n", 1 << (bits % 10), bitsize_prefix(bits));
    printf("\tAddress Bits: %d bank, %d row, %d colum\n",
        SPD_BANK_ADDR_BITS(spd->banks), SPD_ROW_ADDR_BITS(spd->rowcol), SPD_COL_ADDR_BITS(spd->rowcol));
    printf("\tRanks: %d\n", SPD_RANKS(spd->ranks));

    bits = 1 << SPD_DATA_BITS(spd->ecc);
    printf("\tMemory Width: %d bits%s (%dx%d bits)\n", bits, ((spd->ecc & SPD_ECC_BITS) ? " with ECC" : ""),
        bits >> SPD_IO_BITS_PER_CHIP(spd->ranks), 1 << SPD_IO_BITS_PER_CHIP(spd->ranks));

    /* Manufacturing Data */
    /* TODO: Add a verbose flag and put this back in along with the timing data. */
    //printf("\tManufacture ID: %02x%02x\n", spd->mfr_id[1], spd->mfr_id[0]);
    //printf("\tManufacture Location: 0x%02x\n", spd->mfr_location);
    printf("\tManufacture Date: %02x%02x\n", spd->mfr_year, spd->mfr_week);
    printf("\tSerial Number: %02x:%02x:%02x:%02x\n", spd->serial[0], spd->serial[1], spd->serial[2], spd->serial[3]);
} /* print_spd */

static void
print_spd_mtb(const struct ddr3_spd *spd, const char *name, unsigned long mtb, int8_t ftb)
{
    unsigned long picosec = (1000UL * mtb * spd->mtb_dividend) / spd->mtb_divisor;
    if (ftb) {
        picosec += (ftb * SPD_FTB_DIVIDEND(spd->ftb)) / SPD_FTB_DIVISOR(spd->ftb);
    }
    printf("\t%s: %lu ps (%lu mtb)\n", name, picosec, mtb);
}

static void
print_spd_timing(const struct ddr3_spd *spd)
{
    printf("\tMedium Time Base: %d/%d ns\n", spd->mtb_dividend, spd->mtb_divisor);
    printf("\tFine Time Base: %d/%d ps\n", SPD_FTB_DIVIDEND(spd->ftb), SPD_FTB_DIVISOR(spd->ftb));
    print_spd_mtb(spd, "t_ck_min", spd->t_ck_min, 0);
    print_spd_mtb(spd, "t_aa_min", spd->t_aa_min, spd->t_aa_corr);
    print_spd_mtb(spd, "t_wr_min", spd->t_wr_min, 0);
    print_spd_mtb(spd, "t_rcd_min", spd->t_rcd_min, spd->t_rcd_corr);
    print_spd_mtb(spd, "t_rrd_min", spd->t_rrd_min, 0);
    print_spd_mtb(spd, "t_rp_min", spd->t_rp_min, spd->t_rp_corr);
    /* TODO: t_rc, t_ras and t_rfc are multibyte values. */
    print_spd_mtb(spd, "t_wtr_min", spd->t_wtr_min, 0);
    print_spd_mtb(spd, "t_rtp_min", spd->t_rtp_min, 0);
    /* TODO: t_faw is a multibyte value. */
} /* print_spd_timing */

static int
gpio_read(const char *filename)
{
    char buf[2];
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    lseek(fd, 0, SEEK_SET);
    if (read(fd, buf, sizeof(buf)) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return buf[0] == '1';
} /* gpio_read */

static int
do_info(struct fpga *fpga, char *const argv[], int argc)
{
    int fd, err, slot;
    char serial[EEPROM_CAMERA_SERIAL_LEN];

	if ((fd = open(EEPROM_I2C_BUS, O_WRONLY | O_CREAT, 0666)) < 0) {
		fprintf(stderr, "Failed to open i2c bus (%s)\n", strerror(errno));
	    return -1;
    }

    /* Print the serial number */
    err = i2c_eeprom_read16(fd, EEPROM_I2C_ADDR_CAMERA, EEPROM_CAMERA_SERIAL_OFFSET, serial, sizeof(serial));
    if (err < 0) {
        fprintf(stderr, "Failed to read serial number (%s)\n", strerror(errno));
        close(fd);
        return -1;
    }
    sleep(1);
    printf("FPGA Version: %d\n", fpga->reg[FPGA_VERSION]);
    printf("Camera Serial: %.*s\n", sizeof(serial), serial);
    printf("Image Sensor: LUX1310 %s\n", gpio_read(LUX1310_COLOR_DETECT) ? "Color" : "Monochome");

    /* Attempt to read the DDR3 SPD info for slots 0 and 1 */
    for (slot = 0; slot < 2; slot++) {
        struct ddr3_spd spd;
        char sz_readable[32];
        err = i2c_eeprom_read(fd, EEPROM_I2C_ADDR_SPD(slot), 0, &spd, sizeof(spd));
        if (err < 0) {
            continue;
        }
        if (!spd_size_readable(&spd, sz_readable)) {
            printf("\nDetected Unsupported RAM in Slot %d:\n", slot);
        }
        else {
            printf("\nDetected %s RAM in Slot %d:\n", sz_readable, slot);
            print_spd(&spd);
        }
    } /* for */

    close(fd);    
    return 0;
} /* do_info */

/* The eeprom subcommand */
const struct cli_subcmd cli_cmd_info = {
    .name = "info",
    .desc = "Read camera information",
    .function = do_info,
};
