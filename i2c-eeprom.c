/* Copyright 2017 Kron Technologies. All Rights Reserved. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <stddef.h>

#define BYTES_PER_PAGE       256          /* one eeprom page is 256 byte */
#define MAX_BYTES            8            /* max number of bytes to write in one chunk */

static int
i2c_eeprom_write_chunk(int fd, unsigned int addr, uint8_t offset, const void *buf, size_t len, int offsz)
{
    uint8_t wbuf[MAX_BYTES + offsz];
	struct i2c_msg msg = {
        .addr = addr,
        .flags = 0,
        .len = len + offsz,
        .buf = wbuf,
    };
    struct i2c_rdwr_ioctl_data rdwr = {
        .msgs = &msg,
        .nmsgs = 1
    };
    int i;

    for (i = 8 * (offsz - 1); i >= 0; i -= 8) {
        wbuf[i] = (offset >> i) & 0xff;
    };
    memcpy(wbuf + i, buf, len);

    return ioctl(fd, I2C_RDWR, &rdwr);
} /* i2c_eeprom_write_chunk */

static int
i2c_eeprom_read_chunk(int fd, unsigned int addr, void *buf, size_t len)
{
    struct i2c_msg msg = {
        .addr = addr,
        .flags = I2C_M_RD,
        .len = len,
        .buf = buf,
    };
    struct i2c_rdwr_ioctl_data rdwr = {
        .msgs = &msg,
        .nmsgs = 1,
    };
    return ioctl(fd, I2C_RDWR, &rdwr);
} /* i2c_eeprom_read_chunk */

static int
i2c_eeprom_do_write(int fd, unsigned int addr, unsigned int offset, const uint8_t *buf, size_t len, int offsz)
{
	if ((len + offset) > BYTES_PER_PAGE) {
        /* TODO: Support more than one page. */
		fprintf(stderr,"Sorry, len(%d)+offset(%d) > 256 (page boundary)\n", len,offset);
		return -1;
    }
    
    /* Write chunks until the request is satisified. */
    while (len) {
        size_t chunksz = len;
        int err;
        if (chunksz > MAX_BYTES) {
            chunksz = MAX_BYTES;
        }
        err = i2c_eeprom_write_chunk(fd, addr, offset, buf, chunksz, offsz);
        if (err < 0) {
            return err;
        }
        buf += chunksz;
        offset += chunksz;
        len -= chunksz;
    } /* while*/

    return 0;
} /* i2c_eeprom_do_write */

static int
i2c_eeprom_do_read(int fd, unsigned int addr, unsigned int offset, uint8_t *buf, size_t len, int offsz)
{
    int err;
    
	if ((len + offset) > BYTES_PER_PAGE) {
        /* TODO: Support more than one page. */
		fprintf(stderr,"Sorry, len(%d)+offset(%d) > 256 (page boundary)\n", len,offset);
		return -1;
    }

    /* Write zero bytes to set the read pointer. */
    err = i2c_eeprom_write_chunk(fd, addr, offset, NULL, 0, offsz);
    if (err < 0) {
        return -1;
    }

    /* Read chunks until the request is satisified. */
    while (len) {
        size_t chunksz = len;
        if (chunksz > MAX_BYTES) {
            chunksz = MAX_BYTES;
        }
        err = i2c_eeprom_read_chunk(fd, addr, buf, chunksz);
        if (err < 0) {
            return err;
        }
        buf += chunksz;
        offset += chunksz;
        len -= chunksz;
    } /* while*/

    return 0;
} /* i2c_eeprom_do_read */

int
i2c_eeprom_read(int fd, unsigned int addr, unsigned int offset, void *buf, size_t len)
{
    return i2c_eeprom_do_read(fd, addr, offset, buf, len, 1);
} /* i2c_eeprom_read */

int
i2c_eeprom_read16(int fd, unsigned int addr, unsigned int offset, void *buf, size_t len)
{
    return i2c_eeprom_do_read(fd, addr, offset, buf, len, 2);
} /* i2c_eeprom_read16 */
