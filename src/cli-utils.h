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

#define gpio_write(_fd_, _val_) \
    (void)write(_fd_, (_val_) ? "1" : "0", 1)

#endif /* _CLI_UTILS_H */
