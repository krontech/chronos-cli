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
#include <stdint.h>
#include <sys/types.h>

#include "tiff.h"

#define ROUND4(_x_) (((_x_) + 3) & ~0x3)

/* Return the number of bytes to represent the data element of a tag. */
static unsigned int
tiff_tag_datalen(const struct tiff_tag *t)
{
    switch (t->type) {
        case TIFF_TYPE_BYTE:
        case TIFF_TYPE_ASCII:
        case TIFF_TYPE_SBYTE:
        case TIFF_TYPE_UNDEFINED:
            return t->count;
    
        case TIFF_TYPE_SHORT:
        case TIFF_TYPE_SSHORT:
            return (t->count * 2);

        case TIFF_TYPE_LONG:
        case TIFF_TYPE_SLONG:
        case TIFF_TYPE_FLOAT:
            return (t->count * 4);

        case TIFF_TYPE_RATIONAL:
        case TIFF_TYPE_SRATIONAL:
        case TIFF_TYPE_DOUBLE:
            return (t->count * 8);
        
        case TIFF_TYPE_SUBIFD:
            /* HACK: Assuming a count of 1. */
            return tiff_sizeof_ifd(t->data);
    }
    return 0;
}

/* Count the size, in bytes, of an IFD. */
int
tiff_sizeof_ifd(const struct tiff_ifd *ifd)
{
    size_t length = 6;
    uint16_t i;
    for (i = 0; i < ifd->count; i++) {
        unsigned int dlen = tiff_tag_datalen(&ifd->tags[i]);
        length += 12;
        if (dlen > 4) length += ROUND4(dlen);
    }
    return length;
}

static inline unsigned int tiff_put_short(void *p, uint16_t val)
{
    *(uint16_t *)p = val;
    return sizeof(uint16_t);
}

static inline unsigned int
tiff_put_long(void *p, uint32_t val)
{
    *(uint32_t *)p = val;
    return sizeof(uint32_t);
}

size_t
tiff_write_ifd(void *dest, size_t offset, size_t maxlen, const struct tiff_ifd *ifd)
{
    uint8_t *start = dest;
    uint8_t *entry = start + offset;
    uint8_t *extdata;
    unsigned int i;

    /* Write the Image File Directory */
    extdata = entry + ROUND4((12 * ifd->count) + 6);
    if ((extdata - start) >= maxlen) {
        return 0;   /* IFD tags would overflow. */
    }
    entry += tiff_put_short(entry, ifd->count);
    for (i = 0; i < ifd->count; i++) {
        const struct tiff_tag *t = &ifd->tags[i];
        unsigned int datalen = tiff_tag_datalen(t);
        entry += tiff_put_short(entry, t->tag);
        entry += tiff_put_short(entry, t->type & TIFF_TYPE_REAL_MASK);
        entry += tiff_put_long(entry, t->count);

        if (datalen > 4) {
            if (((extdata - start) + datalen) > maxlen) {
                return 0;    /* IFD extended values would overflow.  */
            }
            entry += tiff_put_long(entry, extdata - start);
            if (t->type == TIFF_TYPE_SUBIFD) {
                tiff_write_ifd(dest, (extdata - start), maxlen, t->data);
            }
            else {
                memcpy(extdata, t->data, datalen);
            }
            extdata += ROUND4(datalen);
        } else {
            memcpy(entry, t->data, datalen);
            entry += sizeof(uint32_t);
        }
    }
    entry += tiff_put_long(entry, 0);
    return (extdata - start);
}

void *
tiff_build_header(void *dest, size_t size, const struct tiff_ifd *ifd)
{
    uint8_t *header = dest;
    struct tiff_ifh ifh = {
        .order = {'I', 'I'},
        .magic = 42,
        .offset = sizeof(struct tiff_ifh)
    };
    uint8_t *extdata;
    unsigned int i, offset;
    int err;

    /* Write the Image File Header. */
    memcpy(header, &ifh, sizeof(ifh));
    offset = sizeof(ifh);

    /* Write the Image File Directory. */
    if (!tiff_write_ifd(header, offset, size, ifd)) {
        return NULL;
    }
    return dest;
}
