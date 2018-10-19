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
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/uio.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <setjmp.h>

#include "pwrcmd.h"
#include "mbcrc16.h"

static uint16_t
pwrcmd_crc(uint8_t cmd, const void *buf, size_t len)
{
    uint16_t crc = CRC16_INIT;
    crc = CRC16Iteration(crc, ((len+1) >> 8) & 0xff);
    crc = CRC16Iteration(crc, ((len+1) >> 0) & 0xff);
    crc = CRC16Iteration(crc, cmd);
    if (!len) return crc;
    return CRC16(buf, len, crc);
}

/* Transmit a power command (1-byte value) */
int
pwrcmd_command(int fd, uint8_t cmd)
{
    uint16_t crc = pwrcmd_crc(cmd, NULL, 0);
    uint8_t buf[] = {
        PWRCMD_SOF_VALUE,   /* Start-of-Frame */
        0, 1,               /* Length*/
        cmd,                /* Command */
        (crc >> 8) & 0xff,
        (crc >> 0) & 0xff,
    };
    return write(fd, buf, sizeof(buf));
}

/* Transmit a power command with payload */
int
pwrcmd_transmit(int fd, uint8_t cmd, const void *buf, size_t len)
{
    uint16_t crc = pwrcmd_crc(cmd, buf, len);
    uint16_t footer[] = {
        (crc >> 8) & 0xff,
        (crc >> 0) & 0xff,
    };
    uint8_t header[] = {
        PWRCMD_SOF_VALUE,
        ((len + 1) >> 8) & 0xff,
        ((len + 1) >> 0) & 0xff,
        cmd
    };
    struct iovec iov[] = {
        { .iov_base = header, .iov_len = sizeof(header) },
        { .iov_base = (void *)buf, .iov_len = len },
        { .iov_base = footer, .iov_len = sizeof(footer) }
    };
    return writev(fd, iov, sizeof(iov) / sizeof(struct iovec));
}

/* Get the next character from the input stream, or jump out if we need more data. */
static int
pwrcmd_getchar(int fd, jmp_buf jbuf)
{
    uint8_t ch;
    int ret = read(fd, &ch, 1);
    if (ret == 0) {
        longjmp(jbuf, EAGAIN);
    }
    if (ret < 0) {
        longjmp(jbuf, errno);
    }
    return ch;
}

/*
 * Try reading a command from the power controller, updating offs as we go.
 * Returns:
 *      Positive if a command was successfully received.
 *      Zero if we ran out of input before finding a valid command.
 *      Negative if there was an error reading from the file descriptor.
 */
int
pwrcmd_try_receive(int fd, uint8_t *buf, size_t maxlen, size_t *offs)
{
    jmp_buf jbuf;
    size_t length;
    uint16_t crc;
    int ch;

    /* Return here if we ran out of input. */
    int err = setjmp(jbuf);
    if (err != 0) {
        if (err == EAGAIN) return 0; 
        /* Otherwise a real error occured. */
        errno = err;
        return -1;
    }

    /* Get the minimum command header of SOF + length. */
    switch (*offs) {
        case 0:
            /* Waiting to read the SOF. */
            do {
                ch = pwrcmd_getchar(fd, jbuf);
            } while (ch != PWRCMD_SOF_VALUE);
            buf[(*offs)++] = ch;

            /* Read the length */
        case 1:
            buf[*offs] = pwrcmd_getchar(fd, jbuf);
            (*offs)++;
        case 2:
            buf[*offs] = pwrcmd_getchar(fd, jbuf);
            (*offs)++;
        default:
            length = (buf[1] << 8) | buf[2];
            if ((length + 5) > maxlen) {
                /* Length would overflow the buffer. */
                *offs = 0;
                return 0;
            }
            break;
    }

    /* Read the desired number of bytes and then check the CRC. */
    while ((*offs) < (length + 5)) {
        buf[*offs] = pwrcmd_getchar(fd, jbuf);
        (*offs)++;
    }
    crc = CRC16(buf + 1, length + 2, CRC16_INIT);
    if (((crc >> 8) != buf[length+3]) || ((crc & 0xff) != buf[length+4])) {
        /* CRC failed */
        *offs = 0;
        return 0;
    }

    /* Drop the SOF and length with memmove */
    *offs = 0;
    memmove(buf, buf + 3, length);
    return length;
}

/*
 * Reading a command from the power controller, with a timeout.
 * Returns:
 *      Positive if a command was successfully received.
 *      Zero if we ran out of input before finding a valid command.
 *      Negative if there was an error reading from the file descriptor.
 * 
 * Timeout can take the values:
 *      Positive to wait for at most 'timeout' milliseconds for the command.
 *      Zero to read the command immediately, or return an error.
 *      Negative to block indefinitely for the next valid command.
 */
int
pwrcmd_receive(int fd, uint8_t *buf, size_t maxlen, int timeout)
{
    size_t offset = 0;
    struct timespec end = {0, 0};
    int ret;

    /* Compute how long to wait if a timeout was provided. */
    if (timeout > 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        end.tv_sec = now.tv_sec + timeout / 1000;
        end.tv_nsec = now.tv_nsec + (timeout % 1000) * 1000000;
        if (end.tv_nsec > 1000000000) {
            end.tv_nsec -= 1000000000;
            end.tv_sec++;
        }
    }

    /* Poll for input */
    for (;;) {
        int msec = timeout;
        struct pollfd pfd = {.fd = fd, .events = POLLIN | POLLERR, .revents = 0 };

        /* Recompute the timeout (if finite). */
        if (timeout > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            msec = (end.tv_sec - now.tv_sec) * 1000;
            msec -= (end.tv_nsec / 1000000);
            msec += (now.tv_nsec / 1000000);
            if (msec <= 0) {
                errno = ETIMEDOUT;
                return -1;
            }
        }

        /* Wait for and process input. */
        ret = poll(&pfd, 1, msec);
        if (ret < 0) {
            if (ret == 0) errno = ETIMEDOUT;
            return -1;
        }
        ret = pwrcmd_try_receive(fd, buf, maxlen, &offset);
        if (ret != 0) return ret;
    }
}


/* Parse the battery data command. */
int
pwrcmd_parse_battery(BatteryData *batt, uint8_t *buf, size_t length)
{
    unsigned int i = 1;
    uint8_t cmd = *buf;

    if (length < 11) {
        return -1;
    }
    memset(batt, 0, sizeof(BatteryData));
    batt->battCapacityPercent = buf[i++];
    batt->battSOHPercent = buf[i++];
    batt->battVoltage  = buf[i++] << 8;
    batt->battVoltage |= buf[i++] & 0xFF;
    batt->battCurrent  = buf[i++] << 8;
    batt->battCurrent |= buf[i++] & 0xFF;
    batt->battHiResCap  = buf[i++] << 8;
    batt->battHiResCap |= buf[i++] & 0xFF;
    batt->battHiResSOC  = buf[i++] << 8;
    batt->battHiResSOC |= buf[i++] & 0xFF;

    /* Data only available for the extra command. */
    if ((cmd == PWRCMD_GET_DATA_EXT) && (length >= 19)) {
        batt->battVoltageCam= buf[i++] << 8;
        batt->battVoltageCam |= buf[i++] & 0xFF;
        batt->battCurrentCam= buf[i++] << 8;
        batt->battCurrentCam |= buf[i++] & 0xFF;
        batt->mbTemperature= buf[i++] << 8;
        batt->mbTemperature |= buf[i++] & 0xFF;
        batt->flags = buf[i++];
        batt->fanPWM = buf[i++];
    }
    return 0;
}
