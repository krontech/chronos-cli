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
#include <errno.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <sys/ioctl.h>

#include <linux/types.h>
#include <linux/spi/spidev.h>

#include "fpga.h"
#include "utils.h"

#define FPGA_BITSTREAM_CHUNK    1024

#define FPGA_SPI_CSEL_DELAY     0
#define FPGA_SPI_WORD_BITS      8
#define FPGA_SPI_CLOCK_HZ       33000000
#define FPGA_SPI_MODE           0

/*
 * Lattice TN1260 defines the bitstream preamble as 0xFFFFBDB3, but
 * this has only been observed for unecrypted bitstreams. Other docs
 * suggest that 0xFFFFBAB3 is used for encrypted bitstreams, but we
 * have also seen a lot of 0xFFFFBFB3 in the wild.
 */
#define BITSTREAM_PREAMBLE_MASK     0xFFFFF0FF
#define BITSTREAM_PREAMBLE_STANDARD 0xFFFFBDB3
#define BITSTREAM_PREAMBLE_ENCRYPT  0xFFFFBAB3
#define BITSTREAM_PREAMBLE_ENC2     0xFFFFBFB3
#define BITSTREAM_PREAMBLE_MATCH(_pre_) (((_pre_) & BITSTREAM_PREAMBLE_MASK) == 0xFFFFB0B3)

/* Lattice Bitstream Tags. */
#define BITSTREAM_START_COMMENT 0xff00
#define BITSTREAM_END_COMMENT   0x00ff
#define BITSTREAM_DUMMY         0xFFFFFFFF
#define ECP5_LSC_WRITE_COMP_DIC 0x02
#define ECP5_LSC_RESET_CRC      0x3B
#define ECP5_VERIFY_ID          0xE2
#define ECP5_LSC_PROG_CNTRL0    0x22
#define ECP5_LSC_INIT_ADDRESS   0x46
#define ECP5_LSC_PROG_INCR_CMP  0xB8
#define ECP5_LSC_PROG_SED_CRC   0xA2
#define ECP5_ISC_PROGRAM_SECURITY 0xCE
#define ECP5_ISC_PROGRAM_USERCODE 0xC2
#define ECP5_EBR_ADDRESS        0xF6

/* Lattice ECP5 SPI commands. */
#define ECP5_READ_ID            0xE0
#define ECP5_LSC_READ_STATUS    0x3C
#define ECP5_LSC_ENABLE         0xC6 //This is listed as 0xC3 in the ECP5 SysConfig documenation, which is WRONG
#define ECP5_LSC_DISABLE        0x26
#define ECP5_REFRESH            0x79
#define ECP5_LSC_PROG_INCR_RTI  0x82
#define ECP5_LSC_BITSTREAM_BURST 0x7A
#define ECP5_LSC_PROGRAM_DONE   0x5E

/* ECP5 Device Status Register */
#define ECP5_STATUS_TRANSPARENT     (1 << 0)
#define ECP5_STATUS_CFG_TARGET      (0x7 << 1)
#define ECP5_STATUS_CFG_TARGET_SRAM     (0 << 1)
#define ECP5_STATUS_CFG_TARGET_EFUSE    (1 << 1)
#define ECP5_STATUS_JTAG_ACTIVE     (1 << 4)
#define ECP5_STATUS_PWD_PROTECTION  (1 << 5)
#define ECP5_STATUS_DECRYPT_ENABLE  (1 << 7)
#define ECP5_STATUS_DONE            (1 << 8)
#define ECP5_STATUS_ISC_ENABLE      (1 << 9)
#define ECP5_STATUS_WRITE_ENABLE    (1 << 10)
#define ECP5_STATUS_READ_ENABLE     (1 << 11)
#define ECP5_STATUS_BUSY_FLAG       (1 << 12)
#define ECP5_STATUS_FAIL_FLAG       (1 << 13)
#define ECP5_STATUS_FEA_OTP         (1 << 14)
#define ECP5_STATUS_DECRYPT_ONLY    (1 << 15)
#define ECP5_STATUS_PWD_ENABLE      (1 << 16)
#define ECP5_STATUS_ENCRYPT_PREAMBLE (1 << 20)
#define ECP5_STATUS_STANDARD_PREAMBLE (1 << 21)
#define ECP5_STAUTS_SPIM_FAIL_1     (1 << 22)
#define ECP5_STATUS_BSE_ERROR       (0x7 << 23)
#define ECP5_STATUS_BSE_ERROR_NONE      (0 << 23)
#define ECP5_STATUS_BSE_ERROR_ID        (1 << 23)
#define ECP5_STATUS_BSE_ERROR_CMD       (2 << 23)
#define ECP5_STATUS_BSE_ERROR_CRC       (3 << 23)
#define ECP5_STATUS_BSE_ERROR_PREAMBLE  (4 << 23)
#define ECP5_STATUS_BSE_ERROR_ABORT     (5 << 23)
#define ECP5_STATUS_BSE_ERROR_OVERFLOW  (6 << 23)
#define ECP5_STATUS_BSE_ERROR_SRAM      (7 << 23)
#define ECP5_STATUS_EXECUTION_ERROR (1 << 26)
#define ECP5_STATUS_ID_ERROR        (1 << 27)
#define ECP5_STATUS_INVALID_CMD     (1 << 28)
#define ECP5_STATUS_SED_ERROR       (1 << 29)
#define ECP5_STATUS_BYPASS_MODE     (1 << 30)
#define ECP5_STATUS_FLOW_THROUGH    (1 << 31)

const struct enumval fpga_target_vals[] = {
    {ECP5_STATUS_CFG_TARGET_SRAM,   "SRAM"},
    {ECP5_STATUS_CFG_TARGET_EFUSE,  "Efuse"},
    {0, 0}
};

const struct enumval fpga_bse_vals[] = {
    {ECP5_STATUS_BSE_ERROR_NONE,        "No Error"},
    {ECP5_STATUS_BSE_ERROR_ID,          "ID Error"},
    {ECP5_STATUS_BSE_ERROR_CMD,         "Illegal Command"},
    {ECP5_STATUS_BSE_ERROR_CRC,         "CRC Error"},
    {ECP5_STATUS_BSE_ERROR_PREAMBLE,    "Preamble Error"},
    {ECP5_STATUS_BSE_ERROR_ABORT,       "Configuration Aborted"},
    {ECP5_STATUS_BSE_ERROR_OVERFLOW,    "Data Overflow"},
    {ECP5_STATUS_BSE_ERROR_SRAM,        "Bitstream Exceeds SRAM Array"},
    {0, 0}
};

struct hwpins {
    /* Linux SPI device. */
    int spi;
    /* GPIO pins. */
    int csel;
    int holdn;
    int progn;
    int init;
    int done;
};

static int
hwpins_open(struct hwpins *pins, const struct ioport *iops)
{
    const char *spidevice;
    uint32_t hz = FPGA_SPI_CLOCK_HZ;
    uint8_t mode = FPGA_SPI_MODE;
    uint8_t wordsz = FPGA_SPI_WORD_BITS;

    /* Open the SPI device. */
    if (!(spidevice = ioport_find_by_name(iops, "ecp5-spidev"))) {
        fprintf(stderr, "No ioport definition found for ecp5-spidev\n");
        return -1;
    }
    pins->spi = open(spidevice, O_RDWR);
    if (pins->spi < 0) {
        fprintf(stderr, "Failed to open spidev device: %s\n", strerror(errno));
        return -1;
    }
	if (ioctl(pins->spi, SPI_IOC_WR_MODE, &mode) < 0) {
        fprintf(stderr, "Failed to set SPI write mode: %s\n", strerror(errno));
        close(pins->spi);
        return  -1;
    }
	if (ioctl(pins->spi, SPI_IOC_WR_BITS_PER_WORD, &wordsz) < 0) {
        fprintf(stderr, "Failed to set SPI word size: %s\n", strerror(errno));
        close(pins->spi);
        return  -1;
    }
    if (ioctl(pins->spi, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
        fprintf(stderr, "Failed to set SPI clock speed: %s\n", strerror(errno));
        close(pins->spi);
        return  -1;
    }

    /* FIXME: Needs error handling. */
    pins->csel = open(ioport_find_by_name(iops, "ecp5-cs"), O_WRONLY);
    pins->holdn = open(ioport_find_by_name(iops, "ecp5-holdn"), O_WRONLY);
    pins->progn = open(ioport_find_by_name(iops, "ecp5-progn"), O_WRONLY);
    pins->init = open(ioport_find_by_name(iops, "ecp5-init"), O_RDONLY);
    pins->done = open(ioport_find_by_name(iops, "ecp5-done"), O_RDONLY);
    return 0;
} /* hwpins_open */

static void
hwpins_cleanup(struct hwpins *pins)
{
    if (pins->spi >= 0) close(pins->spi);
    if (pins->csel >= 0) close(pins->csel);
    if (pins->holdn >= 0) close(pins->holdn);
    if (pins->progn >= 0) close(pins->progn);
    if (pins->init >= 0) close(pins->init);
    if (pins->done >= 0) close(pins->done);
}

/* Issue a SPI command to the FPGA. */
static int
fpga_spi_xfer(struct hwpins *pins, uint8_t cmd, const void *tx, void *rx, size_t len)
{
    int ret;
    uint8_t cmdbuf[4] = {cmd, 0, 0, 0};
    struct spi_ioc_transfer msg[2] = {
        {
            .tx_buf = (uintptr_t)cmdbuf,
            .rx_buf = 0,
            .len = sizeof(cmdbuf),
            .speed_hz = FPGA_SPI_CLOCK_HZ,
            .delay_usecs = FPGA_SPI_CSEL_DELAY,
            .bits_per_word = FPGA_SPI_WORD_BITS,
        },
        {
            .tx_buf = (uintptr_t)tx,
            .rx_buf = (uintptr_t)rx,
            .len = len,
            .speed_hz = FPGA_SPI_CLOCK_HZ,
            .delay_usecs = FPGA_SPI_CSEL_DELAY,
            .bits_per_word = FPGA_SPI_WORD_BITS,
        },
    };
    gpio_write(pins->csel, 0);
    ret = ioctl(pins->spi, SPI_IOC_MESSAGE(2), msg);
    gpio_write(pins->csel, 1);
    return ret;
} /* fpga_spi_xfer */

static int
fpga_spi_sendbits(struct hwpins *pins, int bitfd)
{
    int ret;
    uint8_t bits[FPGA_BITSTREAM_CHUNK];
    uint8_t cmdbuf[4] = {ECP5_LSC_BITSTREAM_BURST, 0, 0, 0};
    struct spi_ioc_transfer msg = {
        .tx_buf = (uintptr_t)cmdbuf,
        .rx_buf = 0,
        .len = sizeof(cmdbuf),
        .speed_hz = FPGA_SPI_CLOCK_HZ,
        .delay_usecs = FPGA_SPI_CSEL_DELAY,
        .bits_per_word = FPGA_SPI_WORD_BITS,
    };

    /* Start the write operation. */
    gpio_write(pins->csel, 0);
    ret = ioctl(pins->spi, SPI_IOC_MESSAGE(1), &msg);
    if (ret < 0) {
        gpio_write(pins->csel, 1);
        return ret;
    }

    /* Write the FPGA data out to the device, without releasing the chipselect. */
    msg.tx_buf = (uintptr_t)bits;
    do {
        ret = read(bitfd, bits, sizeof(bits));
        if (ret <= 0) {
            break; /* EOF, or an error occured. */
        }
        msg.len = ret;
        ret = ioctl(pins->spi, SPI_IOC_MESSAGE(1), &msg);
    } while (ret >= 0);
    gpio_write(pins->csel, 1);
    return ret;
} /* fpga_spi_sendbits */

static void
fpga_fprint_status(FILE *stream, uint32_t status)
{
    unsigned long val = status & ECP5_STATUS_CFG_TARGET;
    if (!stream) {
        return;
    }

    fprintf(stream, "\tTransparent Mode: %ld\n", getbits(status, ECP5_STATUS_TRANSPARENT));
    fprintf(stream, "\tConfig Target: %s (%ld)\n", enumval_name(fpga_target_vals, val, "Unknown"), val);
    fprintf(stream, "\tPassword Protect: %ld\n", getbits(status, ECP5_STATUS_PWD_PROTECTION));

    fprintf(stream, "\tEnabled:");
    if (status & ECP5_STATUS_DECRYPT_ENABLE) fprintf(stream, " DECRYPT");
    if (status & ECP5_STATUS_ISC_ENABLE) fprintf(stream, " ISC");
    if (status & ECP5_STATUS_READ_ENABLE) fprintf(stream, " READ");
    if (status & ECP5_STATUS_WRITE_ENABLE) fprintf(stream, " WRITE");
    if (status & ECP5_STATUS_PWD_ENABLE) fprintf(stream, " PWD");

    fprintf(stream, "\n\tStatus:");
    if (status & ECP5_STATUS_JTAG_ACTIVE) fprintf(stream, " JTAG");
    if (status & ECP5_STATUS_DONE) fprintf(stream, " DONE");
    if (status & ECP5_STATUS_BUSY_FLAG) fprintf(stream, " BUSY");
    if (status & ECP5_STATUS_FAIL_FLAG) fprintf(stream, " FAIL");
    if (status & ECP5_STATUS_FEA_OTP) fprintf(stream, " OTP");
    fprintf(stream, "\n");

    fprintf(stream, "\tDecrypt Only: %ld\n", getbits(status, ECP5_STATUS_DECRYPT_ONLY));
    fprintf(stream, "\tEncrypt Preamble: %ld\n", getbits(status, ECP5_STATUS_ENCRYPT_PREAMBLE));
    fprintf(stream, "\tStandard Preamble: %ld\n", getbits(status, ECP5_STATUS_STANDARD_PREAMBLE));
    val = getbits(status, ECP5_STATUS_BSE_ERROR);
    fprintf(stream, "\tBSE Error: %s (%ld)\n", enumval_name(fpga_bse_vals, val, "Unknown"), val);

    fprintf(stream, "\tErrors:");
    if (status & ECP5_STATUS_EXECUTION_ERROR) fprintf(stream, " EXEC");
    if (status & ECP5_STATUS_ID_ERROR) fprintf(stream, " ID");
    if (status & ECP5_STATUS_INVALID_CMD) fprintf(stream, " CMD");
    if (status & ECP5_STATUS_SED_ERROR) fprintf(stream, " SED");
    if (status & ECP5_STAUTS_SPIM_FAIL_1) fprintf(stream, " SPIm");
    
    fprintf(stream, "\n\tBypass Mode: %ld\n", getbits(status, ECP5_STATUS_BYPASS_MODE));
    fprintf(stream, "\tFlow Through: %ld\n", getbits(status, ECP5_STATUS_FLOW_THROUGH));
} /* fpga_fprint_status */

#define logprintf(_log_, ...) if (_log_) fprintf(_log_, __VA_ARGS__)

static uint32_t
fpga_peek_devid(struct hwpins *pins, int bitfd, FILE *log)
{
    uint32_t devid = 0;

    /* Ensure the file is seekable, otherwise just write it blind. */
    if (lseek(bitfd, 0, SEEK_SET) < 0) {
        return 0;
    }

    /* Break out to cleanup. */
    do {
        uint8_t cmd;
        uint16_t tag;
        uint32_t preamble = 0;
        uint32_t dummy;
        
        /* Check for the comment tag at the start of the file. */
        if (read(bitfd, &tag, sizeof(tag)) != sizeof(tag)) break;
        if (be16toh(tag) == BITSTREAM_START_COMMENT) {
            /* Print out comments until we find the end tag. */
            uint16_t etag = 0x0000;
            uint8_t ch;
            int len = 0;

            logprintf(log, "fpga_load: Bitstream Comment Block\n");
            while (read(bitfd, &ch, sizeof(ch)) == sizeof(ch)) {
                etag = (etag << 8) | ch;
                if (etag == BITSTREAM_END_COMMENT) break;
                if (log) {
                    if (len++ == 0) fputc('\t', log);
                    if (ch == '\0') ch = '\n';
                    if (ch == '\n') len = 0;
                    fputc(ch, log);
                }
            }
        } else {
            /* No comment block - start again from the top. */
            lseek(bitfd, 0, SEEK_SET);
        }

        /* Scan forward until we find the preamble. */
        while (!BITSTREAM_PREAMBLE_MATCH(preamble)) {
            uint8_t ch;
            if (read(bitfd, &ch, sizeof(ch)) != sizeof(ch)) break;
            preamble = (preamble << 8) | ch;
        }
        if (!BITSTREAM_PREAMBLE_MATCH(preamble)) {
            logprintf(log, "fpga_load: Bitstream preamble not found\n");
            break;
        }
        if (preamble != BITSTREAM_PREAMBLE_STANDARD) {
            logprintf(log, "fpga_load: Encrypted preamble found: 0x%08x\n", preamble);
            break;
        }
        logprintf(log, "fpga_load: Standard preamble found: 0x%08x\n", preamble);

        /* Expect 32 dummy bits. */
        if (read(bitfd, &dummy, sizeof(dummy)) != sizeof(dummy)) break;
        if (be32toh(dummy) != BITSTREAM_DUMMY) break;

        /* Expect a reset-CRC command. */
        if (read(bitfd, &cmd, sizeof(cmd)) != sizeof(cmd)) break;
        if (cmd != ECP5_LSC_RESET_CRC) break;
        if (lseek(bitfd, 3, SEEK_CUR) < 0) break;

        /* Expect a verify-ID command. */
        if (read(bitfd, &cmd, sizeof(cmd)) != sizeof(cmd)) break;
        if (cmd != ECP5_VERIFY_ID) break;
        if (lseek(bitfd, 3, SEEK_CUR) < 0) break;
        
        /* Get the device ID. */
        if (read(bitfd, &devid, sizeof(devid)) != sizeof(devid)) break;
        devid = be32toh(devid);
        logprintf(log, "fpga_load: Bitstream device ID: 0x%08x\n", devid);
    } while(0);

    lseek(bitfd, 0, SEEK_SET);
    return devid;
}

int
fpga_load(const struct ioport *iops, const char *bitstream, FILE *log)
{
    struct hwpins pins;
    int bitfd;
    int ret;
    uint32_t devid = 0; /* Ignore device-ID check if zero. */
    uint32_t fpgaid;
    uint32_t status;
    
    /* Create the important devices. */
    ret = hwpins_open(&pins, iops);
    if (ret < 0) {
        fprintf(stderr, "fpga_load: Failed to open GPIO pins: %s\n", strerror(errno));
        return -1;
    }
    bitfd = open(bitstream, O_RDONLY);
    if (bitfd < 0) {
        fprintf(stderr, "fpga_load: Failed to open bitstream file (%s): %s\n", bitstream, strerror(errno));
        hwpins_cleanup(&pins);
        return -1;
    }

    /* Peek into the bitstream to find the deviceid, or zero for accept any. */
    devid = fpga_peek_devid(&pins, bitfd, log);

    /* Break out to cleanup. */
    do {
        gpio_write(pins.progn, 1);
        gpio_write(pins.csel, 1);
        gpio_write(pins.holdn, 1);
        usleep(50000); /* TODO: better to just wait for INIT to go high. */

        /* Assert PROGn low to force the FPGA into programming mode. */
        logprintf(log, "fpga_load: Asserting PROGn to enter programming mode.\n");
        gpio_write(pins.progn, 0);
        usleep(1000);
        gpio_write(pins.progn, 1);
        usleep(50000); /* TODO: better to just wait for INIT to go high. */

        /* Read the device ID. */
        ret = fpga_spi_xfer(&pins, ECP5_READ_ID, NULL, &fpgaid, sizeof(fpgaid));
        if (ret < 0) {
            fprintf(stderr, "Failed to read FPGA ID: %s\n", strerror(errno));
            break;
        }
        fpgaid = be32toh(fpgaid);
        if ((devid != 0) && (fpgaid != devid)) {
            fprintf(stderr, "Unsupported FPGA ID: 0x%08x\n", fpgaid);
            break;
        }
        logprintf(log, "fpga_load: Read ECP5 device ID: 0x%08x\n", fpgaid);

        /* Issue a refresh command */
        ret = fpga_spi_xfer(&pins, ECP5_REFRESH, NULL, NULL, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to issue FPGA refresh command: %s\n", strerror(errno));
            break;
        }
        usleep(100000);
        ret = fpga_spi_xfer(&pins, ECP5_LSC_ENABLE, NULL, NULL, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to issue FPGA write enable command: %s\n", strerror(errno));
            break;
        }

        /* Write the FPGA bitstream */
        ret = fpga_spi_sendbits(&pins, bitfd);
        if (ret < 0) {
            fprintf(stderr, "Failed while writing FPGA bitstream: %s\n", strerror(errno));
            break;
        }

        /* Read FPGA status */
        ret = fpga_spi_xfer(&pins, ECP5_LSC_READ_STATUS, NULL, &status, sizeof(fpgaid));
        if (ret < 0) {
            fprintf(stderr, "Failed to read FPGA status: %s\n", strerror(errno));
            break;
        }
        status = be32toh(status);
        logprintf(log, "fpga_load: Device Status: 0x%08x\n", status);
        fpga_fprint_status(log, status);
        if (!(status & ECP5_STATUS_DONE)) {
            fprintf(stderr, "FPGA programming failed: DONE flag not asserted.\n");
            ret = -1;
            break;
        }

        /* Issue a write disable, followed by NOP. */
        ret = fpga_spi_xfer(&pins, ECP5_LSC_DISABLE, NULL, NULL, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to issue FPGA write disable command: %s\n", strerror(errno));
            break;
        }
        /* Success! */
        logprintf(log, "fpga_load: Programming Successful\n");
        ret = 0;
    } while(0);

    close(bitfd);
    hwpins_cleanup(&pins);
    return ret;
} /* fpga_load */

/* Unload the FPGA image and power down. */
int
fpga_unload(const struct ioport *iops)
{
    int progn = ioport_open(iops, "ecp5-progn", O_RDWR);
    char buf[2] = {'0', '\0'}; 
    if (progn < 0) {
        fprintf(stderr, "Failed to open iport for ecp5-progn: %s\n", strerror(errno));
        return progn;
    }
    if (write(progn, "0", 1) <= 0) {
        fprintf(stderr, "Failed to write iport for ecp5-progn: %s\n", strerror(errno));
        close(progn);
        return -1;
    }
    close(progn);
    return 0;
}
