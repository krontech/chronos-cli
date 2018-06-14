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
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/uio.h>
#include <gst/gst.h>

#include "pipeline.h"
#include "utils.h"

/* TIFF Image File Header */
struct tiff_ifh {
    uint8_t order[2];   /* "II" for little-endian or "MM" for big-endian. */
    uint16_t magic;     /* 42 - a very important number. */
    uint32_t offset;    /* Byte offset into the file where the IFD exists. */
};

/* TIFF Image File Directory Entry */
struct tiff_tag_data {
    uint16_t    tag;
    uint16_t    type;
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

/* TIFF Types */
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
#define TIFF_TAG_FLOAT(_tag_, _val_)        TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_FLOAT,    (float[]){_val_})
#define TIFF_TAG_DOUBLE(_tag_, _val_)       TIFF_TAG_SCALAR(_tag_, TIFF_TYPE_DOUBLE,   (double[]){_val_})

/* Specail case for null-terminated strings. */
#define TIFF_TAG_STRING(_tag_, _val_) \
    { .tag = (_tag_), .type = TIFF_TYPE_ASCII, .count = strlen(_val_) + 1, .data = _val_ }

/* Length of data allocated for the Image file directory */
#define TIFF_HEADER_SIZE    4096

/* Returns the number of bytes that a tag overflows by, or zero if it fits in the value. */
static unsigned int
tiff_tag_datalen(const struct tiff_tag_data *t)
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
    }
    return 0;
}

/* Count the number of bytes required for the extra TIFF values. */
int
tiff_count_bytes(const struct tiff_tag_data *tags, uint16_t count)
{
    int extra = 0;
    int i;
    for (i = 0; i < count; i++) {
        unsigned int x = tiff_tag_datalen(&tags[i]);
        if (x > 4) extra += x;
    }
    return extra;
} /* tiff_count_bytes */

#define ROUND4(_x_) (((_x_) + 3) & ~0x3)

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

void *
tiff_build_header(const struct tiff_tag_data *tags, uint16_t count)
{
    /* Static memory buffer for the TIFF header and IFD. */
    static uint8_t header[TIFF_HEADER_SIZE];

    int err;
    struct tiff_ifh ifh = {
        .order = {'I', 'I'},
        .magic = 42,
        .offset = sizeof(struct tiff_ifh)
    };
    uint8_t *extdata;
    unsigned int i, offset;

    /* Write the Image File Header */
    memcpy(header, &ifh, sizeof(ifh));
    offset = sizeof(ifh);

    /* Write the Image File Directory */
    extdata = header + ROUND4(offset + (12 * count) + 6);
    if ((extdata - header) >= TIFF_HEADER_SIZE) {
        return NULL;    /* IFD tags would overflow. */
    }
    offset += tiff_put_short(header + offset, count);
    for (i = 0; i < count; i++) {
        const struct tiff_tag_data *t = &tags[i];
        unsigned int datalen = tiff_tag_datalen(t);
        offset += tiff_put_short(header + offset, t->tag);
        offset += tiff_put_short(header + offset, t->type);
        offset += tiff_put_long(header + offset, t->count);

        if (datalen > 4) {
            if (((extdata - header) + datalen) > TIFF_HEADER_SIZE) {
                return NULL;    /* IFD extended values would overflow.  */
            }
            offset += tiff_put_long(header + offset, extdata - header);
            memcpy(extdata, t->data, datalen);
            extdata += ROUND4(datalen);
        } else {
            memcpy(header + offset, t->data, datalen);
            offset += sizeof(uint32_t);
        }
    }
    offset += tiff_put_long(header + offset, 0);

    return header;
}

static gboolean
dng_probe_greyscale(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buffer);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    struct iovec iov[2];
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    char fname[64];
    int fd;
    
    /* The list of tags we want. */
    const struct tiff_tag_data tags[] = {
        /* TIFF Baseline Tags */
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, xres),           /* ImageWidth */
        TIFF_TAG_LONG(257, yres),           /* ImageLength */
        TIFF_TAG_SHORT(258, 16),            /* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, 1),             /* PhotometricInterpretation = Grayscale */
        TIFF_TAG_STRING(271, "Kron Technologies"), /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),    /* Model */
        TIFF_TAG_LONG(273, TIFF_HEADER_SIZE),   /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),             /* Orientation = Zero/zero is Top Left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, xres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buffer)),    /* StripByteCounts */
        TIFF_TAG_RATIONAL(282, xres, 1),    /* XResolution */
        TIFF_TAG_RATIONAL(283, yres, 1),    /* YResolution */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        /* TODO: Software */
        /* TODO: DateTIme */
    
        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        /* TODO: ColorMatrix1 */
        /* TODO: AsShortNeutral */
        /* TOOD: CalibrationIlluminant */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
    };

    /* Write the frame data. */
    state->dngcount++;
    iov[0].iov_base = tiff_build_header(tags, sizeof(tags)/sizeof(struct tiff_tag_data));
    iov[0].iov_len = TIFF_HEADER_SIZE;
    iov[1].iov_base = GST_BUFFER_DATA(buffer);
    iov[1].iov_len = GST_BUFFER_SIZE(buffer);
    sprintf(fname, "frame_%06lu.dng", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }
    writev(fd, iov, 2);
    close(fd);
    return TRUE;
}

static gboolean
dng_probe_bayer(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buffer);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    struct iovec iov[2];
    const uint8_t cfa_pattern[] = {1, 0, 2, 1}; /* GRBG Bayer pattern */
    const uint16_t cfa_repeat[] = {2, 2};       /* 2x2 Bayer Pattern */
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    char fname[64];
    int fd;
    
    /* The list of tags we want. */
    const struct tiff_tag_data tags[] = {
        /* TIFF Baseline Tags */
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, xres),           /* ImageWidth */
        TIFF_TAG_LONG(257, yres),           /* ImageLength */
        TIFF_TAG_SHORT(258, 16),            /* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, 32803),         /* PhotometricInterpretation = Color Filter Array */
        TIFF_TAG_STRING(271, "Kron Technologies"), /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),    /* Model */
        TIFF_TAG_LONG(273, TIFF_HEADER_SIZE),   /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),             /* Orientation = Zero/zero is Top Left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, xres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buffer)),    /* StripByteCounts */
        TIFF_TAG_RATIONAL(282, xres, 1),    /* XResolution */
        TIFF_TAG_RATIONAL(283, yres, 1),    /* YResolution */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        /* TODO: Software */
        /* TODO: DateTIme */
    
        /* TIFF-EP Tags */
        /* TODO: SubIFD */
        TIFF_TAG(33421, TIFF_TYPE_SHORT, cfa_repeat),   /* CFARepeatPatternDim = 2x2 */
        TIFF_TAG(33422, TIFF_TYPE_BYTE, cfa_pattern),   /* CFAPattern = GRBG */
    
        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        TIFF_TAG_SHORT(50711, 1),                       /* CFALayout = square */
        /* TODO: ColorMatrix1 */
        /* TODO: AsShortNeutral */
        /* TOOD: CalibrationIlluminant */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
    };

    /* Write the frame data. */
    state->dngcount++;
    iov[0].iov_base = tiff_build_header(tags, sizeof(tags)/sizeof(struct tiff_tag_data));
    iov[0].iov_len = TIFF_HEADER_SIZE;
    iov[1].iov_base = GST_BUFFER_DATA(buffer);
    iov[1].iov_len = GST_BUFFER_SIZE(buffer);
    sprintf(fname, "frame_%06lu.dng", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }
    writev(fd, iov, 2);
    close(fd);
    return TRUE;
} /* dng_probe */

GstPad *
cam_dng_sink(struct pipeline_state *state, struct pipeline_args *args, GstElement *pipeline)
{
    GstElement *queue, *sink;
    GstPad *pad;
    int color;
    int flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int ret;

    ret = mkdir(args->filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (ret < 0) {
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, strerror(errno));
        return NULL;
    }

    state->write_fd = open(args->filename, O_RDONLY | O_DIRECTORY);
    if (state->write_fd < 0) {
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, strerror(errno));
        return NULL;
    }
    state->dngcount = 0;

    /* Allocate our segment of the video pipeline. */
    queue =		gst_element_factory_make("queue",		    "dng-queue");
    sink =		gst_element_factory_make("fakesink",		"file-sink");
    if (!queue || !sink) {
        close(state->write_fd);
        return NULL;
    }

    /* Read the color detection pin. */
	pad = gst_element_get_static_pad(queue, "src");
    color = ioport_open(state->iops, "lux1310-color", O_RDONLY);
    if (color < 0) {
	    gst_pad_add_buffer_probe(pad, G_CALLBACK(dng_probe_greyscale), state);
    }
    else if (gpio_read(color)) {
	    gst_pad_add_buffer_probe(pad, G_CALLBACK(dng_probe_bayer), state);
        close(color);
    }
    else {
	    gst_pad_add_buffer_probe(pad, G_CALLBACK(dng_probe_greyscale), state);
        close(color);
    }
	gst_object_unref(pad);

    gst_bin_add_many(GST_BIN(pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
} /* cam_dng_sink */
