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
#include <gst/gst.h>

#include "pipeline.h"
#include "utils.h"
#include "tiff.h"

/* Typical kernel page size. */
#define KPAGE_SIZE          4096
#define TIFF_HDR_SIZE       KPAGE_SIZE

/* Recursive version of mkdir to create an enitre path. */
static int
dng_mkdir(const char *path, mode_t mode)
{
    char pathbuf[PATH_MAX+1];
    char *pathseg;

    /* Copy the string and start parsing it into path segments. */
    if (strlen(path) > PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(pathbuf, path, PATH_MAX);
    pathbuf[PATH_MAX] = '\0';
    pathseg = pathbuf;

    for (;;) {
        /* Parse out the next path segment. */
        char *end;
        while (*pathseg == '/') pathseg++;  /* skip passed leading slashes. */
        end = strchr(pathseg, '/');         /* find the end of the segment. */
        if (!end) break;                    /* handle the last segment outside the loop. */
        *end = '\0';

        /* Assume the parent directories already exist. */
        if ((strcmp(pathseg, ".") == 0) || (strcmp(pathseg, "..") == 0)) {
            *end = '/';
            pathseg = end;
            continue;
        }
        /* Try creating the directory. */
        if (mkdir(pathbuf, mode) != 0) {
            /* It's okay for the directory to exist, but otherwise it's a failure. */
            if (errno != EEXIST) return -1;
        }
        *end = '/';
        pathseg = end;
    }

    /* Create the final directory in the path. */
    return mkdir(path, mode);
}

static gboolean
dng_probe_greyscale(GstPad *pad, GstBuffer *buf, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buf);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    const struct tiff_srational cmatrix[3] = {
        {0, 1}, {1, 1}, {0, 1},
    };
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct video_segment *seg = state->seglist.head;
    
    /* The list of EXIF tags. */
    time_t now = time(0);
    struct tm timebuf;
    char timestr[64];
    size_t timelen = strftime(timestr, sizeof(timestr), "%Y:%m:%d %T", gmtime_r(&now, &timebuf));
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, seg->metadata.exposure, seg->metadata.timebase),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),                         /* ExifVersion = 2.2 */
        TIFF_TAG_VECTOR(36868, TIFF_TYPE_ASCII, timestr, timelen + 1),              /* DateTimeDigitized */
        TIFF_TAG_STRING(42033, state->serial),                                      /* SerialNumber */
        TIFF_TAG_SRATIONAL(51044, seg->metadata.timebase, seg->metadata.interval),  /* FrameRate */
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
        TIFF_TAG_SHORT(262, 34892),         /* PhotometricInterpretation = LinearRaw */
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, TIFF_HDR_SIZE),          /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),                     /* Orientation = Zero is top left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, yres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buf)),   /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        TIFF_TAG_SUBIFD(34665, &exif_ifd),              /* Exif IFD Pointer */

        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
        TIFF_TAG_VECTOR(50721, TIFF_TYPE_SRATIONAL, cmatrix, sizeof(cmatrix)/sizeof(struct tiff_srational)),
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Create the next file in the image sequence. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.dng", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }

    /* Write the header and frame data. */
    tiff_build_header(state->scratchpad, TIFF_HDR_SIZE, &image_ifd);
    memcpy_neon((unsigned char *)state->scratchpad + TIFF_HDR_SIZE, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
    write(fd, state->scratchpad, GST_BUFFER_SIZE(buf) + TIFF_HDR_SIZE);
    close(fd);
    return TRUE;
} /* dng_probe_grayscale */

static gboolean
dng_probe_bayer(GstPad *pad, GstBuffer *buf, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buf);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint8_t cfa_pattern[] = {1, 0, 2, 1}; /* GRBG Bayer pattern */
    const uint16_t cfa_repeat[] = {2, 2};       /* 2x2 Bayer Pattern */
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    const struct tiff_rational wbneutral[3] = {
        {4096, state->fpga->display->wbal[0]},
        {4096, state->fpga->display->wbal[1]},
        {4096, state->fpga->display->wbal[2]}
    };
    const struct tiff_srational cmatrix[9] = {
        // this awkward double-casting is used to preserve the proper sign of the number (it's actually just a 16-bit number with leading zeros even if it's negative)
        {(int32_t)(int16_t)state->fpga->display->ccm_red[0], 4096}, {(int32_t)(int16_t)state->fpga->display->ccm_red[1], 4096}, {(int32_t)(int16_t)state->fpga->display->ccm_red[2], 4096},
        {(int32_t)(int16_t)state->fpga->display->ccm_green[0], 4096}, {(int32_t)(int16_t)state->fpga->display->ccm_green[1], 4096}, {(int32_t)(int16_t)state->fpga->display->ccm_green[2], 4096},
        {(int32_t)(int16_t)state->fpga->display->ccm_blue[0], 4096}, {(int32_t)(int16_t)state->fpga->display->ccm_blue[1], 4096}, {(int32_t)(int16_t)state->fpga->display->ccm_blue[2], 4096}
    };
    char fname[64];
    int fd;

    /* HACK! May not actually correlate to the current frame. */
    struct video_segment *seg = state->seglist.head;

    /* The list of EXIF tags. */
    time_t now = time(0);
    struct tm timebuf;
    char timestr[64];
    size_t timelen = strftime(timestr, sizeof(timestr), "%Y:%m:%d %T", gmtime_r(&now, &timebuf));
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, seg->metadata.exposure, seg->metadata.timebase),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),                         /* ExifVersion = 2.2 */
        TIFF_TAG_VECTOR(36868, TIFF_TYPE_ASCII, timestr, timelen + 1),              /* DateTimeDigitized */
        TIFF_TAG_STRING(42033, state->serial),                                      /* SerialNumber */
        TIFF_TAG_SRATIONAL(51044, seg->metadata.timebase, seg->metadata.interval),  /* FrameRate */
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
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, TIFF_HDR_SIZE),          /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),                     /* Orientation = Zero is top left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, yres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buf)),   /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
    
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
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
        TIFF_TAG_VECTOR(50721, TIFF_TYPE_SRATIONAL, cmatrix, sizeof(cmatrix)/sizeof(struct tiff_srational)),
        TIFF_TAG_VECTOR(50728, TIFF_TYPE_RATIONAL, wbneutral, sizeof(wbneutral)/sizeof(struct tiff_rational)),
        TIFF_TAG_SHORT(50778, 20),                      /* CalibrationIlluminant1 = D55 */
        /* TODO: AsShortNeutral for white balance information. */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Create the next file in the image sequence. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.dng", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }

    /* Write the header and frame data. */
    tiff_build_header(state->scratchpad, TIFF_HDR_SIZE, &image_ifd);
    memcpy_neon((unsigned char *)state->scratchpad + TIFF_HDR_SIZE, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
    write(fd, state->scratchpad, GST_BUFFER_SIZE(buf) + TIFF_HDR_SIZE);
    close(fd);
    return TRUE;
} /* dng_probe_bayer */

GstPad *
cam_dng_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *queue, *sink;
    GstPad *pad;
    int ret;

    ret = dng_mkdir(args->filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ret < 0) {
        strcpy(state->error, strerror(errno));
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, state->error);
        return NULL;
    }

    state->write_fd = open(args->filename, O_RDONLY | O_DIRECTORY);
    if (state->write_fd < 0) {
        strcpy(state->error, strerror(errno));
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, state->error);
        return NULL;
    }
    state->dngcount = 0;

    /* Allocate our segment of the video pipeline. */
    queue = gst_element_factory_make("queue",    "dng-queue");
    sink =  gst_element_factory_make("fakesink", "file-sink");
    if (!queue || !sink) {
        close(state->write_fd);
        strcpy(state->error, "DNG element allocation failure");
        return NULL;
    }

    /* Install the pad callback to generate DNG frames. */
    pad = gst_element_get_static_pad(queue, "src");
    if (state->source.color) {
        gst_pad_add_buffer_probe(pad, G_CALLBACK(dng_probe_bayer), state);
    } else {
        gst_pad_add_buffer_probe(pad, G_CALLBACK(dng_probe_greyscale), state);
    }
    gst_object_unref(pad);

    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
} /* cam_dng_sink */

static gboolean
tiff_probe_grayscale(GstPad *pad, GstBuffer *buf, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buf);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct video_segment *seg = state->seglist.head;
    
    /* The list of EXIF tags. */
    time_t now = time(0);
    struct tm timebuf;
    char timestr[64];
    size_t timelen = strftime(timestr, sizeof(timestr), "%Y:%m:%d %T", gmtime_r(&now, &timebuf));
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, seg->metadata.exposure, seg->metadata.timebase),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),                         /* ExifVersion = 2.2 */
        TIFF_TAG_VECTOR(36868, TIFF_TYPE_ASCII, timestr, timelen + 1),              /* DateTimeDigitized */
        TIFF_TAG_STRING(42033, state->serial),                                      /* SerialNumber */
        TIFF_TAG_SRATIONAL(51044, seg->metadata.timebase, seg->metadata.interval),  /* FrameRate */
    };
    struct tiff_ifd exif_ifd = {.tags = exif, sizeof(exif)/sizeof(struct tiff_tag)};

    /* The list of tags we want. */
    const struct tiff_tag tags[] = {
        /* TIFF Baseline Tags */
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, xres),           /* ImageWidth */
        TIFF_TAG_LONG(257, yres),           /* ImageLength */
        TIFF_TAG_SHORT(258, 8),             /* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, 1),             /* PhotometricInterpretation = Grayscale */
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, TIFF_HDR_SIZE),          /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),                     /* Orientation = Zero is top left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, yres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, xres * yres),    /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        TIFF_TAG_SUBIFD(34665, &exif_ifd),              /* Exif IFD Pointer */
        /* CinemaDNG Tags */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Create the next file in the image sequence. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.tiff", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }

    /* Write the header and frame data. */
    tiff_build_header(state->scratchpad, TIFF_HDR_SIZE, &image_ifd);
    memcpy_rgb2mono((unsigned char *)state->scratchpad + TIFF_HDR_SIZE, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
    write(fd, state->scratchpad, TIFF_HDR_SIZE + (xres * yres));
    close(fd);
    return TRUE;
} /* tiff_probe_grayscale */

static gboolean
tiff_probe_rgb(GstPad *pad, GstBuffer *buf, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buf);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint16_t bpp[] = {8,8,8};
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct video_segment *seg = state->seglist.head;

    /* The list of EXIF tags. */
    time_t now = time(0);
    struct tm timebuf;
    char timestr[64];
    size_t timelen = strftime(timestr, sizeof(timestr), "%Y:%m:%d %T", gmtime_r(&now, &timebuf));
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, seg->metadata.exposure, seg->metadata.timebase),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),                         /* ExifVersion = 2.2 */
        TIFF_TAG_VECTOR(36868, TIFF_TYPE_ASCII, timestr, timelen + 1),              /* DateTimeDigitized */
        TIFF_TAG_STRING(42033, state->serial),                                      /* SerialNumber */
        TIFF_TAG_SRATIONAL(51044, seg->metadata.timebase, seg->metadata.interval),  /* FrameRate */
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
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, TIFF_HDR_SIZE),          /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),                     /* Orientation = Zero is top left */
        TIFF_TAG_SHORT(277, 3),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, yres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buf)),   /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        TIFF_TAG_SUBIFD(34665, &exif_ifd),  /* Exif IFD Pointer */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Create the next file in the image sequence. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.tiff", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create TIFF frame (%s)\n", strerror(errno));
        return TRUE;
    }

    /* Write the header and frame data. */
    tiff_build_header(state->scratchpad, TIFF_HDR_SIZE, &image_ifd);
    memcpy_bgr2rgb((unsigned char *)state->scratchpad + TIFF_HDR_SIZE, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
    write(fd, state->scratchpad, GST_BUFFER_SIZE(buf) + TIFF_HDR_SIZE);
    close(fd);
    return TRUE;
} /* tiff_probe_rgb */

static gboolean
tiff_probe_raw(GstPad *pad, GstBuffer *buf, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    GstCaps *caps = GST_BUFFER_CAPS(buf);
    GstStructure *gstruct = gst_caps_get_structure(caps, 0);
    unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    const uint8_t exif_version[] = {'0', '2', '2', '0'};
    char fname[64];
    int fd;
    /* HACK! May not actually correlate to the current frame. */
    struct video_segment *seg = state->seglist.head;
    
    /* The list of EXIF tags. */
    time_t now = time(0);
    struct tm timebuf;
    char timestr[64];
    size_t timelen = strftime(timestr, sizeof(timestr), "%Y:%m:%d %T", gmtime_r(&now, &timebuf));
    const struct tiff_tag exif[] = {
        TIFF_TAG_RATIONAL(33434, seg->metadata.exposure, seg->metadata.timebase),   /* ExposureTime */
        TIFF_TAG(36864, TIFF_TYPE_UNDEFINED, exif_version),                         /* ExifVersion = 2.2 */
        TIFF_TAG_VECTOR(36868, TIFF_TYPE_ASCII, timestr, timelen + 1),              /* DateTimeDigitize */
        TIFF_TAG_STRING(42033, state->serial),                                      /* SerialNumber */
        TIFF_TAG_SRATIONAL(51044, seg->metadata.timebase, seg->metadata.interval),  /* FrameRate */
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
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, TIFF_HDR_SIZE),          /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),                     /* Orientation = Zero is top left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, yres),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, GST_BUFFER_SIZE(buf)),   /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        TIFF_TAG_SUBIFD(34665, &exif_ifd),              /* Exif IFD Pointer */
        /* CinemaDNG Tags */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
    };
    struct tiff_ifd image_ifd = {.tags = tags, .count = sizeof(tags)/sizeof(struct tiff_tag)};

    /* Create the next file in the image sequence. */
    state->dngcount++;
    sprintf(fname, "frame_%06lu.tiff", state->dngcount);
    fd = openat(state->write_fd, fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create DNG frame (%s)\n", strerror(errno));
        return TRUE;
    }

    /* Write the header and frame data. */
    tiff_build_header(state->scratchpad, TIFF_HDR_SIZE, &image_ifd);
    memcpy_neon((unsigned char *)state->scratchpad + TIFF_HDR_SIZE, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
    write(fd, state->scratchpad, GST_BUFFER_SIZE(buf) + TIFF_HDR_SIZE);
    close(fd);
    return TRUE;
} /* tiff_probe_raw */

GstPad *
cam_tiff_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *queue, *sink;
    GstPad *pad;
    int ret;

    ret = dng_mkdir(args->filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ret < 0) {
        strcpy(state->error, strerror(errno));
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, state->error);
        return NULL;
    }

    state->write_fd = open(args->filename, O_RDONLY | O_DIRECTORY);
    if (state->write_fd < 0) {
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, state->error);
        return NULL;
    }
    state->dngcount = 0;

    /* Allocate our segment of the video pipeline. */
    queue = gst_element_factory_make("queue",    "dng-queue");
    sink =  gst_element_factory_make("fakesink", "file-sink");
    if (!queue || !sink) {
        close(state->write_fd);
        strcpy(state->error, "TIFF element allocation failure");
        return NULL;
    }

    /* Read the color detection pin. */
    pad = gst_element_get_static_pad(queue, "src");
    if (state->source.color) {
        gst_pad_add_buffer_probe(pad, G_CALLBACK(tiff_probe_rgb), state);
    } else {
        gst_pad_add_buffer_probe(pad, G_CALLBACK(tiff_probe_grayscale), state);
    }
    gst_object_unref(pad);

    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
} /* cam_tiff_sink */

GstPad *
cam_tiffraw_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *queue, *sink;
    GstPad *pad;
    int ret;

    ret = dng_mkdir(args->filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ret < 0) {
        strcpy(state->error, strerror(errno));
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, state->error);
        return NULL;
    }

    state->write_fd = open(args->filename, O_RDONLY | O_DIRECTORY);
    if (state->write_fd < 0) {
        fprintf(stderr, "Unable to create directory %s (%s)\n", args->filename, state->error);
        return NULL;
    }
    state->dngcount = 0;

    /* Allocate our segment of the video pipeline. */
    queue = gst_element_factory_make("queue",    "dng-queue");
    sink =  gst_element_factory_make("fakesink", "file-sink");
    if (!queue || !sink) {
        close(state->write_fd);
        strcpy(state->error, "TIFF element allocation failure");
        return NULL;
    }

    /* Read the color detection pin. */
    pad = gst_element_get_static_pad(queue, "src");
    gst_pad_add_buffer_probe(pad, G_CALLBACK(tiff_probe_raw), state);
    gst_object_unref(pad);

    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
}
