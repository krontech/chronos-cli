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

/* Pixel ram is 12-bit packed in big-endian, and we need to write out 16-bit host-endian. */
static int
write_pixels(int fd, const uint8_t *pxdata, size_t len)
{
    size_t i;
#if 0
    for (i = 0; (i+3) <= len; i += 3) {
        uint8_t split = pxdata[i + 1];
        uint16_t outpx[2] = {
            (pxdata[i + 0] << 4) | (split & 0x0f) << 12,
            (pxdata[i + 2] << 8) | (split & 0xf0) << 0,
        };
        if (write(fd, outpx, sizeof(outpx)) < 0) {
            return -1;
        }
    }
#else
    for (i = 0; (i + 24) <= len; i += 24) {
        uint16_t outpx[16];
        neon_be12_unpack(outpx, pxdata + i);
        if (write(fd, outpx, sizeof(outpx)) < 0) {
            return -1;
        }
    }
#endif
    return 0;
}

static int
write_frame(struct fpga *fpga, const char *filename, uint32_t addr, 
    void *(*method)(struct fpga *, void *, uint32_t, uint32_t))
{
    uint8_t tiffbuf[1024];
    uint8_t *framebuf;
    uint8_t is_color = (fpga->display->control & DISPLAY_CTL_COLOR_MODE) != 0;
    uint32_t f_size = fpga->seq->frame_size;
    uint32_t offset;

    const uint8_t cfa_pattern[] = {1, 0, 2, 1}; /* GRBG Bayer pattern */
    const uint16_t cfa_repeat[] = {2, 2};       /* 2x2 Bayer Pattern */
    const uint8_t dng_version[] = {1, 4, 0, 0};
    const uint8_t dng_compatible[] = {1, 0, 0, 0};

    /* TIFF Baseline Tags */
    const struct tiff_tag tags[] = {
        TIFF_TAG_LONG(254, 0),              /* SubFieldType = DNG Highest quality */
        TIFF_TAG_LONG(256, fpga->display->h_res),   /* ImageWidth */
        TIFF_TAG_LONG(257, fpga->display->v_res),   /* ImageLength */
        TIFF_TAG_SHORT(258, 16),            /* BitsPerSample */
        TIFF_TAG_SHORT(259, 1),             /* Compression = None */
        TIFF_TAG_SHORT(262, is_color ? 32803 : 1),  /* PhotometricInterpretation = Color Filter Array or Grayscale */
        TIFF_TAG_STRING(271, "Kron Technologies"),  /* Make */
        TIFF_TAG_STRING(272, "Chronos 1.4"),        /* Model */
        TIFF_TAG_LONG(273, sizeof(tiffbuf)),        /* StripOffsets */
        TIFF_TAG_SHORT(274, 1),             /* Orientation = Zero/zero is Top Left */
        TIFF_TAG_SHORT(277, 1),             /* SamplesPerPixel */
        TIFF_TAG_LONG(278, fpga->display->h_res),           /* RowsPerStrip */
        TIFF_TAG_LONG(279, fpga->display->h_res * 2),       /* StripByteCounts */
        TIFF_TAG_RATIONAL(282, fpga->display->h_res, 1),    /* XResolution */
        TIFF_TAG_RATIONAL(283, fpga->display->v_res, 1),    /* YResolution */
        TIFF_TAG_SHORT(284, 1),             /* PlanarConfiguration = Chunky */
        TIFF_TAG_SHORT(296, 1),             /* ResolutionUnit = None */
        /* TODO: Software */
        /* TODO: DateTime */

        /* Extra tags for the bayer filter on color models. */
        TIFF_TAG(33421, TIFF_TYPE_SHORT, cfa_repeat),   /* CFARepeatPatternDim = 2x2 */
        TIFF_TAG(33422, TIFF_TYPE_BYTE, cfa_pattern),   /* CFAPattern = GRBG */
        TIFF_TAG(50706, TIFF_TYPE_BYTE, dng_version),   /* DNGVersion = 1.4.0.0 */
        TIFF_TAG(50707, TIFF_TYPE_BYTE, dng_compatible),/* DNGBackwardVersion = 1.0.0.0 */
        TIFF_TAG_SHORT(50711, 1),                       /* CFALayout = square */
    };
    struct tiff_ifd image_ifd = {
        .tags = tags,
        .count = sizeof(tags)/sizeof(struct tiff_tag)
    };

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to create file \'%s\': %s\n", filename, strerror(errno));
        return -1;
    }

    if (!is_color) {
        image_ifd.count -= 5; /* Drop the last 5 tags for monochrome sensors. */
    }
    tiff_build_header(tiffbuf, sizeof(tiffbuf), &image_ifd);
    if (write(fd, tiffbuf, sizeof(tiffbuf)) < 0) {
        fprintf(stderr, "Failed to write header \'%s\': %s\n", filename, strerror(errno));
        close(fd);
        return -1;
    }

    framebuf = malloc(f_size * FPGA_FRAME_WORD_SIZE);
    if (!framebuf) {
        fprintf(stderr, "Failed to allocate frame memory for \'%s\': %s\n", filename, strerror(errno));
        close(fd);
        return -1;
    }
    method(fpga, framebuf, addr, f_size);
    write_pixels(fd, framebuf, f_size * FPGA_FRAME_WORD_SIZE);
    close(fd);
    return 0;
}

static void
usage(int argc, char *const argv[])
{
    printf("usage: %s [options]\n\n", argv[0]);
    printf("Attempt video memory recovery, by saving the video RAM to disk as a\n");
    printf("sequence of raw images and calibration data.\n\n");

    printf("options:\n");
    printf("  -d, --dest DIR    save video data to DIR (default: /media/sda1/recovery)\n");
    printf("  -f, --force       ignore existing file, possibly overwriting data\n");
    printf("  --help            display this message and exit\n");
} /* usage */

int
main(int argc, char *const argv[])
{
    int force = 0;
    const char *outdir = "/media/sda1/recovery";
	const char *shortopts = "hd:f";
	const struct option options[] = {
        {"dest",    required_argument,  NULL, 'd'},
        {"force",   no_argument,        NULL, 'f'},
		{"help",    no_argument,        NULL, 'h'},
		{0, 0, 0, 0}
	};
	int ret, fd;
    void *(*vram_readout_func)(struct fpga *, void *, uint32_t, uint32_t) = vram_readout_slow;

    /* Useful FPGA registers. */
    struct fpga *fpga;
    uint16_t version;
    uint16_t subver;
    /* Video region to dump. */
    uint32_t f_size;
    uint32_t r_start;
    uint32_t r_stop;
    uint32_t i;
    char filename[PATH_MAX];

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

            case 'h':
                usage(argc, argv);
                break;
		}
	}

    /* Map the FPGA register space, and inspect its configuration. */
    fpga = fpga_open();
    if (!fpga) {
        fprintf(stderr, "Failed to open FPGA register space: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Step 1) Ensure that we can retreieve a sane FPGA version. */
    printf("Checking FPGA configuration:\n");
    version = fpga->reg[FPGA_VERSION];
    subver = fpga->reg[FPGA_SUBVERSION];
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
    printf("\n");

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

    /* Step 2) Backup the calibration data. */
    printf("Backing up FPN Calibration to %s\n", mkfilepath(filename, outdir, "/fpn.dng"));
    write_frame(fpga, filename, fpga->display->fpn_address, vram_readout_func);
    
    /* Step 3) Begin dumping frames. */
    for (i = 0; i < ((r_stop - r_start) / f_size); i++) {
        uint32_t f_addr = r_start + (f_size * i);
        
        mkfilepath(filename, outdir, "/frame_%06d.dng", i);
        printf("Backing up frame to %s\n", filename);
        write_frame(fpga, filename, f_addr, vram_readout_func);
    }
} /* main */
