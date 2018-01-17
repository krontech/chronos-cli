/****************************************************************************
 *  Copyright (C) 2017-2018 Kron Technologies Inc <http://www.krontech.ca>. *
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
#ifndef _IOPORT_H
#define _IOPORT_H

#include <string.h>
#include <stdint.h>

struct ioport {
    const char *name;
    const char *value;
};
extern const struct ioport board_chronos14_ioports[];

/* Find an IO port configuration by name */
static inline const char *
ioport_find_by_name(const struct ioport *iops, const char *name)
{
    while (iops->name) {
        if (strcmp(name, iops->name) == 0) return iops->value;
        iops++;
    }
    return NULL;
}

#endif /* _IOPORT_H */
