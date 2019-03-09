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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ftw.h>

#include "fpga.h"
#include "tiff.h"
#include "utils.h"

/* Working memory size limits. */
#define MAX_HRES    4096
#define MAX_VRES    4096

static char *
mkfilepath(char *outbuf, const char *dir, const char *format, ...)
{
    va_list args;
    size_t dirlen = strlen(dir);
    memcpy(outbuf, dir, dirlen);

    va_start(args, format);
    vsnprintf(outbuf + dirlen, PATH_MAX - dirlen, format, args);
    va_end(args);
    outbuf[PATH_MAX-1] = '\0';
    return outbuf;
}

static int
force_unlink(const char *pathname, const struct stat *st, int type)
{
    if (!S_ISDIR(st->st_mode)) {
        int ret = unlink(pathname);
        if (ret < 0) {
            fprintf(stderr, "Failed to remove file %s: %s\n", pathname, strerror(errno));
        }
        return ret;
    }
    return 0;
}

static int 
force_rmdir(const char *pathname, const struct stat *st, int type)
{
    if (S_ISDIR(st->st_mode)) {
        int ret = rmdir(pathname);
        if (ret < 0) {
            fprintf(stderr, "Failed to remove directory %s: %s\n", pathname, strerror(errno));
        }
        return ret;
    }
    return 0;
}

/* Dump memory from a given word address and size. */
static void *
vram_readout_slow(struct fpga *fpga, void *dest, uint32_t addr, uint32_t nwords)
{
    uint8_t *out = dest;
    uint32_t end = addr + nwords;
    uint32_t pagewords = 4096 / FPGA_FRAME_WORD_SIZE;

    while (addr < end) {
        /* Set the offset. */
        fpga->reg[GPMC_PAGE_OFFSET + 0] = (addr & 0x0000ffff) >> 0;
        fpga->reg[GPMC_PAGE_OFFSET + 1] = (addr & 0xffff0000) >> 16;

        /* Copy memory out. */
        if ((addr + pagewords) > end) {
            memcpy(out, (void *)fpga->ram, FPGA_FRAME_WORD_SIZE * (end - addr));
            break;
        } else {
            memcpy(out, (void *)fpga->ram, FPGA_FRAME_WORD_SIZE * pagewords);
            addr += pagewords;
            out += pagewords * FPGA_FRAME_WORD_SIZE;
        }
    }
    return dest;
}

/* I suspect this can *only* read 2kB aligned */
static void *
vram_readout_fast(struct fpga *fpga, void *dest, uint32_t addr, uint32_t nwords)
{
    uint8_t *out = dest;
    uint32_t end = addr + nwords;
    uint32_t burstsize = sizeof(fpga->vram->buffer) / FPGA_FRAME_WORD_SIZE;
    
    fpga->vram->burst = 0x20;
    while (addr < end) {
        int i;

        /* Instruct the FPGA to copy the data into cache. */
        fpga->vram->address = addr;
        fpga->vram->control = VRAM_CTL_TRIG_READ;
        for (i = 0; i < 1000; i++) {
            if (fpga->vram->control == 0) break;
        }

        /* Copy memory out. */
        if ((addr + burstsize) >= end) {
            memcpy(out, (void *)fpga->vram->buffer, FPGA_FRAME_WORD_SIZE * (end - addr));
            break;
        } else {
            memcpy(out, (void *)fpga->vram->buffer, FPGA_FRAME_WORD_SIZE * burstsize);
            addr += burstsize;
            out += burstsize * FPGA_FRAME_WORD_SIZE;
        }
    }
    return dest;
}

/* Some statically allocated calibration data. */
static int      cal_npoints = 0;
static int16_t  cal_fpn[MAX_HRES * MAX_VRES];
static int16_t  cal_offset[MAX_HRES];
static uint16_t cal_gain[MAX_HRES];
static int16_t  cal_curve[MAX_HRES]; 

/* Pixel ram is 12-bit packed in big-endian, and we need to write out 16-bit host-endian. */
static int
write_pixels(int fd, const uint8_t *pxdata, size_t hres, size_t vres)
{
    size_t pix, col;
    uint16_t outpx[16];

    if (!cal_npoints) {
        /* No calibration data, just unpack and write to disk. */
        for (pix = 0; (pix + 16) <= (hres * vres); pix += 16) {
            neon_be12_unpack_unsigned(outpx, pxdata);
            if (write(fd, outpx, sizeof(outpx)) < 0) {
                return -1;
            }
            pxdata += 24;
        }
    }
    else if (cal_npoints < 3) {
        /* 2-point calibration data is present, unpack and calibrate */
        for (pix = 0, col = 0; (pix + 16) <= (hres * vres); pix += 16, col += 16) {
            if (col >= hres) col %= hres;
            neon_be12_unpack_2point(outpx, pxdata, cal_fpn + pix, cal_gain + col);
            if (write(fd, outpx, sizeof(outpx)) < 0) {
                return -1;
            }
            pxdata += 24;
        }
    }
    else {
        /* 3-point calibration data is present, unpack and calibrate */
        for (pix = 0, col = 0; (pix + 16) <= (hres * vres); pix += 16, col += 16) {
            if (col >= hres) col %= hres;
            neon_be12_unpack_3point(outpx, pxdata, cal_fpn + pix, cal_offset + col, cal_gain + col, cal_curve + col);
            if (write(fd, outpx, sizeof(outpx)) < 0) {
                return -1;
            }
            pxdata += 24;
        }
    }
    return 0;
}

static int
write_frame(struct fpga *fpga, const char *filename, uint32_t addr, 
    void *(*readout)(struct fpga *, void *, uint32_t, uint32_t))
{
    uint8_t tiffbuf[1024];
    uint8_t *framebuf;
    uint8_t is_color = (fpga->display->control & DISPLAY_CTL_COLOR_MODE) != 0;
    size_t f_size = fpga->display->h_res * fpga->display->v_res * 2;
    uint32_t offset;
    struct tiff_ifd ifd;

    const uint8_t cfa_pattern[] = {1, 0, 2, 1}; /* GRBG Bayer pattern */
    const uint16_t cfa_repeat[] = {2, 2};       /* 2x2 Bayer Pattern */
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};
    struct tiff_rational wbneutral[3] = {
        {4096, fpga->display->wbal[0]},
        {4096, fpga->display->wbal[1]},
        {4096, fpga->display->wbal[2]}
    };
    const struct tiff_srational cmatrix[9] = {
        /* CIE XYZ to LUX1310 color space conversion matrix. */
        {17716, 10000}, {-5404, 10000}, {-1674, 10000},
        {-2845, 10000}, {12494, 10000}, {247,   10000},
        {-2300, 10000}, {6236,  10000}, {6471,  10000}
    };
    const struct tiff_srational mmatrix[3] = {
        {0, 1}, {1, 1}, {0, 1},
    };

    /* TIFF Baseline Tags (color) */
    const struct tiff_tag colortags[] = {
        /* TIFF Baseline Tags */
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, fpga->display->h_res),   /* ImageWidth */
        TIFF_TAG_LONG(257, fpga->display->v_res),   /* ImageLength */
        TIFF_TAG_SHORT(258, 16),            /* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, 32803),         /* PhotometricInterpretation = Color Filter Array */
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, sizeof(tiffbuf)),        /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),             /* Orientation = Zero/zero is Top Left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, fpga->display->v_res),   /* RowsPerStrip */
        TIFF_TAG_LONG(279, f_size),         /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
    
        /* TIFF-EP Tags */
        TIFF_TAG(33421, TIFF_TYPE_SHORT, cfa_repeat),   /* CFARepeatPatternDim = 2x2 */
        TIFF_TAG(33422, TIFF_TYPE_BYTE, cfa_pattern),   /* CFAPattern = GRBG */
    
        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        TIFF_TAG_SHORT(50711, 1),                       /* CFALayout = square */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
        TIFF_TAG_VECTOR(50721, TIFF_TYPE_SRATIONAL, cmatrix, sizeof(cmatrix)/sizeof(struct tiff_srational)),
        TIFF_TAG_VECTOR(50728, TIFF_TYPE_RATIONAL, wbneutral, sizeof(wbneutral)/sizeof(struct tiff_rational)),
        TIFF_TAG_SHORT(50778, 20),                      /* CalibrationIlluminant1 = D55 */
    };

    /* TIFF Baseline Tags (monochrome) */
    const struct tiff_tag monotags[] = {
        /* TIFF Baseline Tags */
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, fpga->display->h_res),   /* ImageWidth */
        TIFF_TAG_LONG(257, fpga->display->v_res),   /* ImageLength */
        TIFF_TAG_SHORT(258, 16),            /* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, 34892),         /* PhotometricInterpretation = LinearRaw */
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, sizeof(tiffbuf)),        /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),             /* Orientation = Zero/zero is Top Left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, fpga->display->v_res),   /* RowsPerStrip */
        TIFF_TAG_LONG(279, f_size),         /* StripByteCounts */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */

        /* CinemaDNG Tags */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_STRING(50708, "Krontech Chronos 1.4"), /* UniqueCameraModel */
        TIFF_TAG_SHORT(50717, 0xfff),                   /* WhiteLevel = 12-bit */
        TIFF_TAG_VECTOR(50721, TIFF_TYPE_SRATIONAL, mmatrix, sizeof(mmatrix)/sizeof(struct tiff_srational)),
    };

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create file \'%s\': %s\n", filename, strerror(errno));
        return -1;
    }

    if (is_color) {
        ifd.tags = colortags;
        ifd.count = sizeof(colortags)/sizeof(struct tiff_tag);
    } else {
        ifd.tags = monotags;
        ifd.count = sizeof(monotags)/sizeof(struct tiff_tag);
    }
    tiff_build_header(tiffbuf, sizeof(tiffbuf), &ifd);
    if (write(fd, tiffbuf, sizeof(tiffbuf)) < 0) {
        fprintf(stderr, "Failed to write header \'%s\': %s\n", filename, strerror(errno));
        close(fd);
        return -1;
    }

    framebuf = malloc(f_size);
    if (!framebuf) {
        fprintf(stderr, "Failed to allocate frame memory for \'%s\': %s\n", filename, strerror(errno));
        close(fd);
        return -1;
    }
    readout(fpga, framebuf, addr, (f_size + FPGA_FRAME_WORD_SIZE - 1) / FPGA_FRAME_WORD_SIZE);
    write_pixels(fd, framebuf, fpga->display->h_res, fpga->display->v_res);
    free(framebuf);
    close(fd);
    return 0;
}

static int
load_caldata(struct fpga *fpga, void *(*readout)(struct fpga *, void *, uint32_t, uint32_t))
{
    size_t in, out;
    uint32_t f_size = fpga->seq->frame_size * FPGA_FRAME_WORD_SIZE;
    uint8_t *cal_fpn_raw = malloc(f_size * FPGA_FRAME_WORD_SIZE);

    /* Grab FPN and column gain. */
    memcpy(cal_gain, (uint8_t *)fpga->reg + FPGA_COL_GAIN_BASE, sizeof(uint16_t) * fpga->display->h_res);
    readout(fpga, cal_fpn_raw, fpga->display->fpn_address, fpga->seq->frame_size);


    if (fpga->display->gainctl & DISPLAY_GAINCTL_3POINT) {
        cal_npoints = 3;

        /* Convert FPN to signed 16-bit data for 2-point calibration. */
        for (in = 0, out = 0; (in + 24) <= f_size; in += 24, out += 16) {
            neon_be12_unpack_signed(cal_fpn + out, cal_fpn_raw + in);
        }
        
        /* Allocate memory for 3-point calibration data. */
        memcpy(cal_offset, (uint8_t *)fpga->reg + FPGA_COL_OFFSET_BASE, sizeof(int16_t) * fpga->display->h_res);
        memcpy(cal_curve,  (uint8_t *)fpga->reg + FPGA_COL_CURVE_BASE,  sizeof(int16_t) * fpga->display->h_res);
    }
    else {
        cal_npoints = 2;

        /* Convert FPN to unsigned 16-bit data for 2-point calibration. */
        for (in = 0, out = 0; (in + 24) <= f_size; in += 24, out += 16) {
            neon_be12_unpack(cal_fpn + out, cal_fpn_raw + in);
        }
    }
    free(cal_fpn_raw);
    return 0;
}

/*===============================================
 * Recording Region Management
 *===============================================
 */
/* Playback regions are stored as a double-linked list. */
struct playback_region {
    struct playback_region *next;
    struct playback_region *prev;
    /* Size and starting address of the recorded frames. */
    unsigned long   size;
    unsigned long   base;
    unsigned long   offset;
    unsigned long   framesz;
};

struct playback_region_list {
    struct playback_region *head;
    struct playback_region *tail;
};

/* Test if two regions overlap each other.  */
static int
playback_region_overlap(struct playback_region *a, struct playback_region *b)
{
    /* Sort a and b. */
    if (a->base > b->base) {struct playback_region *tmp = a; a = b; b = tmp;}
    return ((a->base + a->size) > b->base);
}

static void
playback_region_delete(struct playback_region_list *list, struct playback_region *r)
{
    if (r->next) r->next->prev = r->prev;
    else list->tail = r->prev;
    if (r->prev) r->prev->next = r->next;
    else list->head = r->next;
    free(r);
}

static struct playback_region_list *
load_segments(struct fpga *fpga)
{ 
    int i;
    unsigned int startblock;
    struct playback_region *region;
    struct playback_region_list *list;

    /* If the recording segment data block is absent, then there's nothing to recover. */
    if (fpga->segments->identifier != SEGMENT_IDENTIFIER) {
        return NULL;
    }

    /* Allocate some memory to hold the region list. */
    list = malloc(sizeof(struct playback_region_list));
    if (!list) {
        return NULL;
    }
    list->head = NULL;
    list->tail = NULL;

    /* Start from the oldest block. */
    startblock = fpga->segments->blockno;
    if (startblock >= ARRAY_SIZE(fpga->segments->data)) {
        startblock -= ARRAY_SIZE(fpga->segments->data);
    } else {
        startblock = 0;
    }

    /* Process all segments with sane block numbers, in the order that they were
     * captured by the FPGA to rebuild the video segment data. */
    for (i = 0; i < ARRAY_SIZE(fpga->segments->data); i++) {
        int index = (startblock + i) % ARRAY_SIZE(fpga->segments->data);
        struct fpga_segment_entry entry; 
        memcpy(&entry, (void *)&fpga->segments->data[index], sizeof(entry));

        /* Ignore any blocks of zero size, or nonsensical block number. */
        if (entry.start >= entry.end) continue;
        if (entry.last > entry.end) continue;
        if (entry.last < entry.start) continue;
        if ((entry.data & SEGMENT_DATA_BLOCKNO) < startblock) continue;
        if ((entry.data & SEGMENT_DATA_BLOCKNO) > fpga->segments->blockno) continue;

        /* Ignore recording events within the live display or calibration regions. */
        if (entry.start == fpga->display->fpn_address) continue;
        if (entry.start == fpga->seq->live_addr[0]) continue;
        if (entry.start == fpga->seq->live_addr[1]) continue;
        if (entry.start == fpga->seq->live_addr[2]) continue;

        /* Prepare memory for the new region. */
        region = malloc(sizeof(struct playback_region));
        if (!region) {
            break;
        }
        region->framesz = fpga->seq->frame_size;
        region->base = entry.start;
        region->size = (entry.end - entry.start) + region->framesz;
        region->offset = (entry.last >= entry.end) ? 0 : (entry.last - entry.start + region->framesz);

        /* Free any regions that would overlap. */
        while (list->head) {
            if (!playback_region_overlap(region, list->head)) break;
            playback_region_delete(list, list->head);
        }

        /* Link this region into the end of the list. */
        region->next = NULL;
        region->prev = list->tail;
        if (region->prev) {
            region->prev->next = region;
            list->tail = region;
        }
        else {
            list->tail = region;
            list->head = region;
        }
    }

    /* List the video segments. */
    printf("Found Video Segments:\n");
    for (i = 0, region = list->head; region; region = region->next, i++) {
        printf("Segment %3d: start=0x%08lx offset=0x%08lx length=%lu frames\n", i,
                region->base, region->offset, region->size / region->framesz);
    }

    return list;
}

static void
usage(int argc, char *const argv[])
{
    printf("usage: %s [options]\n\n", argv[0]);
    printf("Attempt video memory recovery, by saving the video RAM to disk as a\n");
    printf("sequence of raw images and calibration data.\n\n");

    printf("options:\n");
    printf("  -d, --dest DIR    save video data to DIR (default: /media/sda1/recovery)\n");
    printf("  -f, --force       ignore existing files, possibly overwriting data\n");
    printf("  -a, --all         record all video memory (ignores segment data)\n");
    printf("  --help            display this message and exit\n");
} /* usage */

int
main(int argc, char *const argv[])
{
    int force = 0;
    int allmem = 0;
    const char *outdir = "/media/sda1/recovery";
	const char *shortopts = "had:f";
	const struct option options[] = {
        {"dest",    required_argument,  NULL, 'd'},
        {"force",   no_argument,        NULL, 'f'},
        {"all",     no_argument ,       NULL, 'a'},
		{"help",    no_argument,        NULL, 'h'},
		{0, 0, 0, 0}
	};
	int ret, fd;
    void *(*vram_readout_func)(struct fpga *, void *, uint32_t, uint32_t);

    /* Useful FPGA registers. */
    struct fpga *fpga;
    uint16_t version;
    uint16_t subver;
    /* Video region to dump. */
    uint32_t f_size;
    uint32_t r_start;
    uint32_t r_stop;
    uint32_t frameno;
    uint32_t segstart;
    uint32_t i;
    char filename[PATH_MAX];

    /* Video region data. */
    struct playback_region *region;
    struct playback_region_list *list;

    /* Map the FPGA register space, and inspect its configuration. */
    fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA register space: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

	/*
	 * Parse command line options until we encounter the first positional
	 * argument, which denodes the subcommand we want to execute. This
	 * would be to handle stuff relating to how we connect to the FPGA.
	 */
	optind = 1;
	while ((optind < argc) && (*argv[optind] == '-')) {
		int c = getopt_long(argc, argv, shortopts, options, NULL);
		if (c < 0) {
			/* End of options */
			break;
		}
		switch (c) {
            case 'd':
                outdir = optarg;
                break;
            
            case 'f':
                force = 1;
                break;
            
            case 'a':
                allmem = 1;
                break;

            case 'h':
                usage(argc, argv);
                return 0;
		}
	}


    /* Step 1) Ensure that we can retreieve a sane FPGA version. */
    printf("Checking FPGA configuration:\n");
    version = fpga->config->version;
    subver = fpga->config->subver;
    f_size = fpga->seq->frame_size;
    r_start = fpga->seq->region_start;
    r_stop = fpga->seq->region_stop;
    printf("\tFPGA Version: %u.%u\n", version, subver);
    printf("\tDisplay Resolution: %ux%u\n", fpga->display->h_res, fpga->display->v_res);
    printf("\tFrame Size: %u\n", f_size * FPGA_FRAME_WORD_SIZE);
    printf("\tFPN Address: 0x%08x\n", fpga->display->fpn_address);
    printf("\tStart Address: 0x%08x\n", r_start);
    printf("\tStop Address: 0x%08x\n", r_stop);
    printf("\tTotal Frames: %u\n", (r_stop - r_start) / f_size);
    if (fpga->vram->identifier == VRAM_IDENTIFIER) {
        printf("\tFast VRAM Readout: Supported\n");
        vram_readout_func = vram_readout_fast;
    } else {
        printf("\tFast VRAM Readout: Unsupported\n");
        vram_readout_func = vram_readout_slow;
    }
    if (fpga->display->gainctl & DISPLAY_GAINCTL_3POINT) {
        printf("\tCalibration: 3-Point\n");
    } else {
        printf("\tCalibration: 2-Point\n");
    }
    printf("\n");

    /* Step 2) Extract the calibration data. */
    load_caldata(fpga, vram_readout_func);

    /* Step 3) Generate the recording segment data. */
    if (!allmem) {
        list = load_segments(fpga);
    }
    if (!list) {
        /* Fake out the segment data with the entire recording region. */
        static struct playback_region fake_region;
        static struct playback_region_list fake_list;

        fake_region.next = NULL;
        fake_region.prev = NULL;
        fake_region.size = fpga->seq->region_stop - fpga->seq->region_start;
        fake_region.base = fpga->seq->region_start;
        fake_region.offset = 0;
        fake_region.framesz = fpga->seq->frame_size;
        
        fake_list.head = &fake_region;
        fake_list.tail = &fake_region;
        list = &fake_list;

        printf("No Segment Data Found - Extracting All Memory\n");
    }

    /* Create a directory for the recovery data. */
    printf("Creating video recovery directory at %s\n", outdir);
    if (force) {
        /* Cowardly refuse to delete root or the current working directory. */
        getcwd(filename, sizeof(filename));
        if ((strcmp(filename, outdir) != 0) && (strcmp("/", outdir) != 0)) {
            ftw(outdir, force_unlink, 10);
            ftw(outdir, force_rmdir, 10);
        }
        ret = mkdir(outdir, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if ((ret < 0) && (errno != EEXIST)) {
            fprintf(stderr, "Unable to create directory %s (%s)\n", outdir, strerror(errno));
            return EXIT_FAILURE;
        }
    }
    else if (mkdir(outdir, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) < 0) {
        fprintf(stderr, "Unable to create directory %s (%s)\n", outdir, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Step 4) Begin dumping frames. */
    frameno = 0;
    for (region = list->head; region; region = region->next) {
        uint32_t seglength = region->size / region->framesz;
        for (i = 0; i < seglength; i++) {
            uint32_t reladdr = (region->offset + i * region->framesz) % region->size;
            uint32_t frameaddr = region->base + reladdr;

            mkfilepath(filename, outdir, "/frame_%06d.dng", frameno++);
            printf("Backing up frame from 0x%08x to %s\n", frameaddr, filename);
            write_frame(fpga, filename, frameaddr, vram_readout_func);
        }
    }
} /* main */
