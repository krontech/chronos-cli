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
#include <sys/mman.h>
#include <gst/gst.h>

#include "pipeline.h"
#include "utils.h"

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

#define ROUND4(_x_) (((_x_) + 3) & ~0x3)

/* Length of data allocated for the Image file directory */
#define TIFF_HEADER_SIZE    4096

/* Typical kernel page size. */
#define KPAGE_SIZE          4096

int tiff_sizeof_ifd(const struct tiff_ifd *ifd);

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
tiff_build_header(const struct tiff_ifd *ifd)
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

    /* Write the Image File Header. */
    memcpy(header, &ifh, sizeof(ifh));
    offset = sizeof(ifh);

    /* Write the Image File Directory. */
    if (!tiff_write_ifd(header, offset, sizeof(header), ifd)) {
        return NULL;
    }
    return header;
}

/* Write a DNG file without compression. */
void
dng_write(int fd, const void *header, GstBuffer *buffer)
{
#if 1
    write(fd, header, TIFF_HEADER_SIZE);
    uint8_t *kpage = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (kpage == MAP_FAILED) {
        fprintf(stderr, "mmap() failed: %s\n", strerror(errno));
        return;
    }
    const uint8_t *data = GST_BUFFER_DATA(buffer);
    int size = GST_BUFFER_SIZE(buffer);
    while (size > 4096) {
        memcpy_neon(kpage, data, 4096);
        write(fd, kpage, 4096);
        data += 4096;
        size -= 4096;
    }
    if (size) {
        memcpy_neon(kpage, data, size);
        write(fd, kpage, size);
    }
    munmap(kpage, 4096);
#else
    struct iovec iov[2] = {
        { .iov_base = (void *)header, .iov_len = TIFF_HEADER_SIZE, },
        { .iov_base = GST_BUFFER_DATA(buffer), .iov_len = GST_BUFFER_SIZE(buffer), },
    };
    writev(fd, iov, 2);
#endif
} /* dng_write */

static gboolean
dng_probe_greyscale(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buffer);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct playback_region *region = state->region_head;
    
    /* The list of EXIF tags. */
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, region->exposure, FPGA_TIMEBASE_HZ),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),             /* ExifVersion = 2.2 */
        TIFF_TAG_SRATIONAL(51044, FPGA_TIMEBASE_HZ, region->interval),  /* FrameRate */
        //TIFF_TAG_STRING(36868, ???),      /* DateTimeDigitized = YYYY:MM:DD HH:MM:SS */
    };
    struct tiff_ifd exif_ifd = {.tags = exif, sizeof(exif)/sizeof(struct tiff_tag)};

    /* The list of tags we want. */
    const struct tiff_tag tags[] = {
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
        TIFF_TAG_SUBIFD(34665, &exif_ifd),              /* Exif IFD Pointer */

        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        /* TODO: ColorMatrix1 */
        /* TODO: AsShortNeutral */
        /* TOOD: CalibrationIlluminant */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
        //TIFF_TAG_STRING(50735, "???"),                /* CameraSerialNumber */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Write the frame data. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.dng", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }
    dng_write(fd, tiff_build_header(&image_ifd), buffer);
    close(fd);
    return TRUE;
} /* dng_probe_grayscale */

static gboolean
dng_probe_bayer(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buffer);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint8_t cfa_pattern[] = {1, 0, 2, 1}; /* GRBG Bayer pattern */
    const uint16_t cfa_repeat[] = {2, 2};       /* 2x2 Bayer Pattern */
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct playback_region *region = state->region_head;

    /* The list of EXIF tags. */
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, region->exposure, FPGA_TIMEBASE_HZ),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),             /* ExifVersion = 2.2 */
        TIFF_TAG_SRATIONAL(51044, FPGA_TIMEBASE_HZ, region->interval),  /* FrameRate */
        //TIFF_TAG_STRING(36868, ???),      /* DateTimeDigitized = YYYY:MM:DD HH:MM:SS */
    };
    struct tiff_ifd exif_ifd = {.tags = exif, sizeof(exif)/sizeof(struct tiff_tag)};
    
    /* The list of tags we want. */
    const struct tiff_tag tags[] = {
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
        /* TODO: SubIFD for preview images */
        TIFF_TAG(33421, TIFF_TYPE_SHORT, cfa_repeat),   /* CFARepeatPatternDim = 2x2 */
        TIFF_TAG(33422, TIFF_TYPE_BYTE, cfa_pattern),   /* CFAPattern = GRBG */
        TIFF_TAG_SUBIFD(34665, &exif_ifd),              /* Exif IFD Pointer */
    
        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        TIFF_TAG_SHORT(50711, 1),                       /* CFALayout = square */
        /* TODO: ColorMatrix1 */
        /* TODO: AsShortNeutral */
        /* TOOD: CalibrationIlluminant */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
        //TIFF_TAG_STRING(50735, "???"),                /* CameraSerialNumber */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Write the frame data. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.dng", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }
    dng_write(fd, tiff_build_header(&image_ifd), buffer);
    close(fd);
    return TRUE;
} /* dng_probe_bayer */

static void
cam_dng_done(struct pipeline_state *state, const struct pipeline_args *args)
{
    /* Flush all files to disk. */
    sync();
}

GstPad *
cam_dng_sink(struct pipeline_state *state, struct pipeline_args *args)
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

    state->done = cam_dng_done;

    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
} /* cam_dng_sink */

/* Write a TIFF file without compression. */
void
tiff_rgb_write(int fd, const void *header, GstBuffer *buffer)
{
    write(fd, header, TIFF_HEADER_SIZE);
    uint8_t *kpage = mmap(NULL, 3 * KPAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (kpage == MAP_FAILED) {
        fprintf(stderr, "mmap() failed: %s\n", strerror(errno));
        return;
    }
    const uint8_t *data = GST_BUFFER_DATA(buffer);
    int size = GST_BUFFER_SIZE(buffer);
    while (size > (3 * KPAGE_SIZE)) {
        memcpy_bgr2rgb(kpage, data, 3 * KPAGE_SIZE);
        write(fd, kpage, 3 * KPAGE_SIZE);
        data += (3 * KPAGE_SIZE);
        size -= (3 * KPAGE_SIZE);
    }
    if (size) {
        memcpy_bgr2rgb(kpage, data, size);
        write(fd, kpage, size);
    }
    munmap(kpage, 3 * KPAGE_SIZE);
} /* tiff_rgb_write */

static gboolean
tiff_probe_rgb(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buffer);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint16_t bpp[] = {8,8,8};
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct playback_region *region = state->region_head;

    /* The list of EXIF tags. */
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, region->exposure, FPGA_TIMEBASE_HZ),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),             /* ExifVersion = 2.2 */
        TIFF_TAG_SRATIONAL(51044, FPGA_TIMEBASE_HZ, region->interval),  /* FrameRate */
        //TIFF_TAG_STRING(36868, ???),      /* DateTimeDigitized = YYYY:MM:DD HH:MM:SS */
    };
    struct tiff_ifd exif_ifd = {.tags = exif, sizeof(exif)/sizeof(struct tiff_tag)};
    
    /* The list of tags we want. */
    const struct tiff_tag tags[] = {
        /* TIFF Baseline Tags */
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, xres),           /* ImageWidth */
        TIFF_TAG_LONG(257, yres),           /* ImageLength */
        TIFF_TAG(258, TIFF_TYPE_SHORT, bpp),/* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, 2),             /* PhotometricInterpretation = RGB */
        TIFF_TAG_STRING(271, "Kron Technologies"), /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),    /* Model */
        TIFF_TAG_LONG(273, TIFF_HEADER_SIZE),   /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),             /* Orientation = Zero/zero is Top Left */
        TIFF_TAG_SHORT(277, 3),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, xres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buffer)),    /* StripByteCounts */
        TIFF_TAG_RATIONAL(282, xres, 1),    /* XResolution */
        TIFF_TAG_RATIONAL(283, yres, 1),    /* YResolution */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        /* TODO: Software */
        /* TODO: DateTIme */
        TIFF_TAG_SUBIFD(34665, &exif_ifd),                  /* Exif IFD Pointer */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Write the frame data. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.tiff", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create TIFF frame (%s)\n", strerror(errno));
        return TRUE;
    }
    tiff_rgb_write(fd, tiff_build_header(&image_ifd), buffer);
    close(fd);
    return TRUE;
} /* tiff_probe_rgb */

GstPad *
cam_tiff_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *queue, *sink;
    GstPad *pad;
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
	gst_pad_add_buffer_probe(pad, G_CALLBACK(tiff_probe_rgb), state);
	gst_object_unref(pad);

    state->done = cam_dng_done;

    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
} /* cam_tiff_sink */

