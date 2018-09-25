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
#ifndef __TIFF_H
#define __TIFF_H

#include <stdint.h>
#include <sys/types.h>

/* TIFF Image File Header */
struct tiff_ifh {
    uint8_t order[2];   /* "II" for little-endian or "MM" for big-endian. */
    uint16_t magic;     /* 42 - a very important number. */
    uint32_t offset;    /* Byte offset into the file where the IFD exists. */
};

/* TIFF Image File Directory */
struct tiff_ifd {
    const struct tiff_tag *tags;
    uint16_t    count;
};

/* TIFF Image File Directory Entry */
struct tiff_tag {
    uint16_t    tag;
    uint32_t    type;
    uint32_t    count;
    const void  *data;
};

struct tiff_rational {
    uint32_t n;
    uint32_t d;
};
struct tiff_srational {
    int32_t n;
    int32_t d;
};

/* Real TIFF Types */
#define TIFF_TYPE_BYTE      1
#define TIFF_TYPE_ASCII     2
#define TIFF_TYPE_SHORT     3
#define TIFF_TYPE_LONG      4
#define TIFF_TYPE_RATIONAL  5
#define TIFF_TYPE_SBYTE     6
#define TIFF_TYPE_UNDEFINED 7
#define TIFF_TYPE_SSHORT    8
#define TIFF_TYPE_SLONG     9
#define TIFF_TYPE_SRATIONAL 10
#define TIFF_TYPE_FLOAT     11
#define TIFF_TYPE_DOUBLE    12

/* Internal TIFF Types */
#define TIFF_TYPE_REAL_MASK 0xffff
#define TIFF_TYPE_SUBIFD    ((1 << 16) | TIFF_TYPE_LONG)

/* General TIFF tag - points to an array of the given type. */
#define TIFF_TAG(_tag_, _type_, _array_) \
    { .tag = (_tag_), .type = (_type_), .count = sizeof(_array_)/sizeof((_array_)[0]), .data = (_array_) }

/* Scalar TIFF tags - contains a single instance of the given type. */
#define TIFF_TAG_SCALAR(_tag_, _type_, _array_) \
    { .tag = (_tag_), .type = (_type_), .count = 1, .data = (_array_) }
#define TIFF_TAG_BYTE(_tag_, _val_)         TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_BYTE,     (uint8_t[]){_val_})
#define TIFF_TAG_SHORT(_tag_, _val_)        TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_SHORT,    (uint16_t[]){_val_})
#define TIFF_TAG_LONG(_tag_, _val_)         TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_LONG,     (uint32_t[]){_val_})
#define TIFF_TAG_RATIONAL(_tag_, _n_, _d_)  TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_RATIONAL, ((struct tiff_rational[]){{_n_, _d_}}))
#define TIFF_TAG_SBYTE(_tag_, _val_)        TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_SBYTE,    (int8_t[]){_val_})
#define TIFF_TAG_SSHORT(_tag_, _val_)       TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_SSHORT,   (int16_t[]){_val_})
#define TIFF_TAG_SLONG(_tag_, _val_)        TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_SLONG,    (int32_t[]){_val_})
#define TIFF_TAG_SRATIONAL(_tag_, _n_, _d_) TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_SRATIONAL,((struct tiff_srational[]){{_n_, _d_}}))
#define TIFF_TAG_FLOAT(_tag_, _val_)        TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_FLOAT,    (float[]){_val_})
#define TIFF_TAG_DOUBLE(_tag_, _val_)       TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_DOUBLE,   (double[]){_val_})
#define TIFF_TAG_SUBIFD(_tag_, _ifd_ptr_)   TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_SUBIFD,   _ifd_ptr_)

/* Specail case for null-terminated strings. */
#define TIFF_TAG_STRING(_tag_, _val_) \
    { .tag = (_tag_), .type = TIFF_TYPE_ASCII, .count = strlen(_val_) + 1, .data = _val_ }

void *tiff_build_header(void *dest, size_t size, const struct tiff_ifd *ifd);

int tiff_sizeof_ifd(const struct tiff_ifd *ifd);
size_t tiff_write_ifd(void *dest, size_t offset, size_t maxlen, const struct tiff_ifd *ifd);


#endif /* __TIFF_H */
