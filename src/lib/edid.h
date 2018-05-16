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
#ifndef __EDID_H
#define __EDID_H

#include <stdint.h>
#include <stdio.h>

#define EDID_PATTERN {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}

/* Digital input flags */
#define EDID_INPUT_F_DIGITAL    (1 << 7)
#define EDID_INPUT_F_DEPTH      (0x7 << 4)
#define EDID_INPUT_F_DEPTH_UNDEF    (0 << 4) 
#define EDID_INPUT_F_DEPTH_8BIT     (2 << 4)
#define EDID_INPUT_F_DEPTH_10BIT    (3 << 4)
#define EDID_INPUT_F_DEPTH_12BIT    (4 << 4)
#define EDID_INPUT_F_DEPTH_14BIT    (5 << 4)
#define EDID_INPUT_F_DEPTH_16BIT    (6 << 4)
#define EDID_INPUT_F_IFACE      (0xF << 0)
#define EDID_INPUT_F_IFACE_UNDEF    (0 << 0)
#define EDID_INPUT_F_IFACE_HDMIa    (1 << 0)
#define EDID_INPUT_F_IFACE_HDMIb    (2 << 0)
#define EDID_INPUT_F_IFACE_MDDI     (4 << 0)
#define EDID_INPUT_F_IFACE_DP       (5 << 0)
/* Analog input flags */
#define EDID_INPUT_F_ANALOG     (0 << 7)
#define EDID_INPUT_F_LEVELS     (0x3 << 5)
#define EDID_INPUT_F_LEVELS_0700_0300   (0 << 5)    /* +0.7V to -0.3V */
#define EDID_INPUT_F_LEVELS_0714_0286   (1 << 5)    /* +0.714V to -0.286V */
#define EDID_INPUT_F_LEVELS_1000_0400   (2 << 5)    /* +1.0V to -0.4V */
#define EDID_INPUT_F_LEVELS_0700_0000   (3 << 5)    /* +0.7V to 0.0V */
#define EDID_INPUT_F_BLANK2BLACK    (1 << 4)
#define EDID_INPUT_F_SEPARATE_SYNC  (1 << 3)
#define EDID_INPUT_F_COMPOSITE_SYNC (1 << 2)
#define EDID_INPUT_F_GREEN_SYNC     (1 << 1)
#define EDID_INPUT_F_VSYNC_SERRATED (1 << 0)

/* Features flags. */
#define EDID_FEATURE_DPMS_STANDBY       (1 << 7)
#define EDID_FEATURE_DPMS_SUSPEND       (1 << 6)
#define EDID_FEATURE_DPMS_ACTIVE_OFF    (1 << 5)
#define EDID_FEATURE_TYPE               (0x3 << 3)
#define EDID_FEATURE_TYPE_RGB               (0 << 3)    /* Digital displays only */
#define EDID_FEATURE_TYPE_RGB_Y444          (1 << 3)    /* Digital displays only */
#define EDID_FEATURE_TYPE_RGB_Y422          (2 << 3)    /* Digital displays only */
#define EDID_FEATURE_TYPE_RGB_Y444_Y222     (3 << 3)    /* Digital displays only */
#define EDID_FEATURE_TYPE_MONOCHROME        (0 << 3)    /* Analog displays only */
#define EDID_FEATURE_TYPE_RGB_COLOR         (1 << 3)    /* Analog displays only */
#define EDID_FEATURE_TYPE_NON_RGB_COLOR     (2 << 3)    /* Analog displays only */
#define EDID_FEATURE_TYPE_UNDEFIEND         (3 << 3)    /* Analog displays only */
#define EDID_FEATURE_SRGB               (1 << 2)
#define EDID_FEATURE_PREFERRED_TIMING   (1 << 1)
#define EDID_FEATURE_CONTIGUOUS_TIMING  (1 << 0)

/* Standard timing information bitmask */
#define EDID_TIMING_ASPECT          (0x3 << 14)
#define EDID_TIMING_ASPECT_16_10        (0 << 14)
#define EDID_TIMING_ASPECT_4_3          (1 << 14)
#define EDID_TIMING_ASPECT_5_4          (2 << 14)
#define EDID_TIMING_ASPECT_16_9         (3 << 14)
#define EDID_TIMING_FREQUENCY_MASK  (0x3F << 8)
#define EDID_TIMING_FREQUENCY_GET(_x_)  ((((_x_) & EDID_TIMING_FREQUENCY_MASK) >> 8) + 60)
#define EDID_TIMING_FREQUENCY_SET(_x_)  ((((_x_) - 60) << 8) & EDID_TIMING_FREQUENCY_MASK)
#define EDID_TIMING_XRES_MASK       (0xFF << 0)
#define EDID_TIMING_XRES_GET(_x_)       ((((_x_) & EDID_TIMING_XRES_MASK) + 31) * 8)
#define EDID_TIMING_XRES_SET(_x_)       ((((_x_) / 8) - 31) & EDID_TIMING_XRES_MASK)

#define EDID_TIMING_UNUSED          0x0101

struct edid_detail {
    uint16_t pxclock;       /* Pixel clock in 10kHz units. */
    uint8_t hactive_lsb;    /* horizontal active pixels. */
    uint8_t hblank_lsb;     /* horizontal blanking interval in pixels. */
    uint8_t hpixel_msb;     /* msbits for horizontal active and blanking intervals. */
    uint8_t vactive_lsb;    /* vertical active lines. */
    uint8_t vblank_lsb;     /* vertical blanking interval in lines. */
    uint8_t vlines_msb;     /* msbits for vertical active and blanking intervals. */
    uint8_t hporch_lsb;     /* horizontal front porch in pixels. */
    uint8_t hsync_lsb;      /* horizontal sync pulse width in pixels. */
    uint8_t vsync_lsb;      /* vertical front porch and sync pulse width in lines. */
    uint8_t sync_msb;       /* assorted msbits for porch and sync pulse timing. */
    uint8_t hsize_lsb;      /* horizontal screen size in mm. */
    uint8_t vsize_lsb;      /* vertical screen size in mm. */
    uint8_t size_msb;       /* msbits for screen size. */
    uint8_t hborder;        /* horizontal border pixels (per side) */
    uint8_t vborder;        /* vertical border pixels (per side) */
    uint8_t features;
};

struct edid_data {
    /* Header Information  */
    uint8_t header[8]; /* Must contain {00, ff, ff, ff, ff, ff, ff, 00} */
    uint16_t mfrid;
    uint16_t pcode;
    uint32_t serial;
    uint8_t mfr_week;
    uint8_t mfr_year;
    uint8_t ver_major;
    uint8_t ver_minor;
    /* Basic display parameters */
    uint8_t in_flags;
    uint8_t h_size;
    uint8_t v_size;
    uint8_t gamma;
    uint8_t f_flags;
    /* Chromaticity coordinates */
    uint8_t chroma_lsb;
    uint8_t bluewhite_lsb;
    uint8_t red_xmsb;
    uint8_t red_ymsb;
    uint8_t green_xmsb;
    uint8_t green_ymsb;
    uint8_t blue_xmsb;
    uint8_t blue_ymsb;
    uint8_t white_xmsb;
    uint8_t white_ymsb;
    /* Established timing bitmap */
    uint8_t common_timings[3];
    /* Standard timings information */
    uint16_t timing[8];
    struct edid_detail detail[4];
    uint8_t num_extensions;
    uint8_t checksum;
};

#define EDID_MAX_TIMINGS    12

uint8_t edid_checksum(const void *data);
int edid_sanity(const void *data);
void edid_fprint(const void *edid, FILE *stream);
unsigned int edid_get_timing(const void *data, int pref, unsigned int *hres, unsigned int *vres);

#endif /* __EDID_H */
