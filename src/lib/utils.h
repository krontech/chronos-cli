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
#ifndef _CLI_UTILS_H
#define _CLI_UTILS_H

#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_x_) (sizeof(_x_)/sizeof((_x_)[0]))
#endif

#ifndef OFFSET_OF
#define OFFSET_OF(_type_, _member_) (size_t)(&((_type_ *)0)->_member_)
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(_p_, _type_, _member_) \
    (_type_ *)((char *)(_p_) - OFFSET_OF(_type_, _member_))
#endif

#ifndef min
#define min(_a_, _b_) \
    ((_a_) > (_b_)) ? (_a_) : (_b_)
#endif

#ifndef max
#define max(_a_, _b_) \
    ((_a_) > (_b_)) ? (_a_) : (_b_)
#endif

/* Bit hacking to extract a value from a bitmask and shift it down. */
static inline unsigned long
getbits(unsigned long value, unsigned long mask)
{
    if (!mask) {
        return 0;
    }
    else {
        unsigned long lsb = (~mask + 1) & mask;
        return (value & mask) / lsb;
    }
} /* getbits */

/* Bit hacking to shift a value up to the position specified by the mask.  */
static inline unsigned long
setbits(unsigned long value, unsigned long mask)
{
    if (!mask) {
        return 0;
    }
    else {
        unsigned long lsb = (~mask + 1) & mask;
        return (value * lsb) & mask;
    }
} /* setbits */

/* Used to store arrays of name/value pairs for UI helpers. */
struct enumval {
    unsigned long value;
    const char *name;
};

static inline const char *
enumval_name(const struct enumval *list, unsigned long value, const char *unknown)
{
    while (list->name) {
        if (list->value == value) {
            return list->name;
        }
        list++;
    }
    return unknown;
} /* enumval_name */

static inline int
gpio_read(int fd)
{
    char buf[2];
    lseek(fd, 0, SEEK_SET);
    if (read(fd, buf, sizeof(buf)) < 0) {
        return -1;
    }
    return buf[0] == '1';
} /* gpio_read */

static inline int
gpio_write(int fd, int val)
{
    return write(fd, val ? "1" : "0", 1);
} /* gpio_write */

#ifdef __arm__
void memcpy_neon(void *dest, const void *src, size_t len);
void memcpy_bgr2rgb(void *dst, const void *src, size_t len);
void memcpy_sum16(void *dest, const void *src, size_t len);

void neon_div16(void *framebuf, size_t len);
#endif

#endif /* _CLI_UTILS_H */
