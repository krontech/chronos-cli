

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
#include <endian.h>

#include "utils.h"
#include "edid.h"

const struct enumval edid_bit_depths[] = {
    {.value = EDID_INPUT_F_DEPTH_UNDEF, .name = "Undefined" },
    {.value = EDID_INPUT_F_DEPTH_8BIT,  .name = "8-bit" },
    {.value = EDID_INPUT_F_DEPTH_10BIT, .name = "10-bit" },
    {.value = EDID_INPUT_F_DEPTH_12BIT, .name = "12-bit" },
    {.value = EDID_INPUT_F_DEPTH_16BIT, .name = "16-bit" },
    { 0, 0 },
};

const struct enumval edid_interfaces[] = {
    {.value = EDID_INPUT_F_IFACE_UNDEF, .name = "Undefined" },
    {.value = EDID_INPUT_F_IFACE_HDMIa, .name = "HDMIa" },
    {.value = EDID_INPUT_F_IFACE_HDMIb, .name = "HDMIb" },
    {.value = EDID_INPUT_F_IFACE_MDDI,  .name = "MDDI" },
    {.value = EDID_INPUT_F_IFACE_DP,    .name = "Display Port" },
    { 0, 0 },
};

const struct enumval edid_analog_levels[] = {
    {.value = EDID_INPUT_F_LEVELS_0700_0300, .name = "+0.7V/-0.3V" },
    {.value = EDID_INPUT_F_LEVELS_0714_0286, .name = "+0.714V/-0.286V" },
    {.value = EDID_INPUT_F_LEVELS_1000_0400, .name = "+1.0V/-0.4V" },
    {.value = EDID_INPUT_F_LEVELS_0700_0000, .name = "+0.7V/0.0V" },
    { 0, 0 },
};

const struct enumval edid_digital_types[] = {
    { .value = EDID_FEATURE_TYPE_RGB,           "RGB" },
    { .value = EDID_FEATURE_TYPE_RGB_Y444,      "RGB-YUV444" },
    { .value = EDID_FEATURE_TYPE_RGB_Y422,      "RGB-YUV422" },
    { .value = EDID_FEATURE_TYPE_RGB_Y444_Y222, "RGB-YUV444-YUV422" },
    { 0, 0 },
};

const struct enumval edid_analog_types[] = {
    { .value = EDID_FEATURE_TYPE_MONOCHROME,    "Monochrome" },
    { .value = EDID_FEATURE_TYPE_RGB_COLOR,     "RGB Color" },
    { .value = EDID_FEATURE_TYPE_NON_RGB_COLOR, "Non-RGB Color" },
    { .value = EDID_FEATURE_TYPE_UNDEFIEND,     "Undefined" },
    { 0, 0 },
};

const struct enumval edid_common_timings[] = {
    { 0, "800x600 @ 60Hz" },
    { 1, "800x600 @ 56Hz" },
    { 2, "640x480 @ 75Hz" },
    { 3, "640x480 @ 72Hz" },
    { 4, "640x480 @ 67Hz" },
    { 5, "640x480 @ 60Hz" },
    { 6, "720x400 @ 88Hz" },
    { 7, "720x400 @ 70Hz" },
    { 8, "1280x1024 @ 75Hz" },
    { 9, "1024x768 @ 75Hz" },
    { 10, "1024x768 @ 72Hz" },
    { 11, "1024x768 @ 60Hz" },
    { 12, "1024x768 @ 87Hz" },
    { 13, "832x624 @ 75Hz" },
    { 14, "800x600 @ 75Hz" },
    { 15, "800x600 @ 72Hz" },
    { 16, "1152x870 @ 75Hz"},
    { 0, 0 },
};

static inline void
edid_fprintf_boolean(FILE *fp, const char *name, int bval)
{
    fprintf(fp, "\t%s: %s\n", name, bval ? "true" : "false");
}


uint8_t
edid_checksum(const void *data)
{
    int i;
    const uint8_t *pdata = data;
    uint8_t sum = 0;
    for (i = 0; i < sizeof(struct edid_data); i++) {
        sum += pdata[i];
    }
    return sum;
}

int
edid_sanity(const void *data)
{
    const struct edid_data *edid = data;
    const uint8_t pattern[] = EDID_PATTERN;
    return (memcmp(pattern, edid->header, sizeof(edid->header)) == 0) && (edid_checksum(data) == 0);
}

unsigned int
edid_get_timing(const void *data, int pref, unsigned int *hres, unsigned int *vres)
{
    const struct edid_data *edid = data;
    uint16_t timing;
    if ((pref < 0) || (pref >= ARRAY_SIZE(edid->timing))) {
        return 0;
    }
    timing = htole16(edid->timing[pref]);
    *hres = EDID_TIMING_XRES_GET(timing);
    switch (timing & EDID_TIMING_ASPECT) {
        case EDID_TIMING_ASPECT_16_10:
            *vres = (*hres * 10) / 16;
            break;
        case EDID_TIMING_ASPECT_4_3:
            *vres = (*hres * 3) / 4;
            break;
        case EDID_TIMING_ASPECT_5_4:
            *vres = (*hres * 4) / 5;
            break;
        case EDID_TIMING_ASPECT_16_9:
        default:
            *vres = (*hres * 9) / 16;
            break;
    }
    return EDID_TIMING_FREQUENCY_GET(timing);
}

static void
edid_fprintf_header(FILE *stream, const struct edid_data *edid)
{
    uint16_t mfrid = htobe16(edid->mfrid);
    fprintf(stream, "\tHeader: [%02x %02x %02x %02x %02x %02x %02x %02x]\n",
            edid->header[0], edid->header[1], edid->header[2], edid->header[3],
            edid->header[4], edid->header[5], edid->header[6], edid->header[7]);
    
    fprintf(stream, "\tProduct Code: %c%c%c\n",
            ((mfrid >> 10) & 0x1F) + 'A' - 1,
            ((mfrid >> 5) & 0x1F) + 'A' - 1,
            ((mfrid >> 0) & 0x1F) + 'A' - 1);

    fprintf(stream, "\tProduct Code: 0x%04x\n", htole16(edid->pcode));
    fprintf(stream, "\tSerial Number: 0x%08x\n", htole32(edid->serial));
    fprintf(stream, "\tDate Code: %02d%02d\n", edid->mfr_year, edid->mfr_week);
    fprintf(stream, "\tEDID Version: %d.%d\n", edid->ver_major, edid->ver_minor);
}

static void
edid_fprintf_basic(FILE *stream, const struct edid_data *edid)
{
    fprintf(stream, "\tInput Type: %s\n", edid->in_flags & EDID_INPUT_F_DIGITAL ? "Digital" : "Analog");
    if (edid->in_flags & EDID_INPUT_F_DIGITAL) {
        fprintf(stream, "\tBit Depth: %s\n", enumval_name(edid_bit_depths, edid->in_flags & EDID_INPUT_F_DEPTH, "Unknown"));
        fprintf(stream, "\tInterface: %s\n", enumval_name(edid_interfaces, edid->in_flags & EDID_INPUT_F_IFACE, "Unknown"));
    } else {
        fprintf(stream, "\tAnalog Levels: %s\n", enumval_name(edid_analog_levels, edid->in_flags & EDID_INPUT_F_LEVELS, "Unknown"));
        edid_fprintf_boolean(stream, "Blank-to-Black", edid->in_flags & EDID_INPUT_F_BLANK2BLACK);
        edid_fprintf_boolean(stream, "Separate Sync", edid->in_flags & EDID_INPUT_F_SEPARATE_SYNC);
        edid_fprintf_boolean(stream, "Composite Sync", edid->in_flags & EDID_INPUT_F_COMPOSITE_SYNC);
        edid_fprintf_boolean(stream, "Sync-on-Green", edid->in_flags & EDID_INPUT_F_GREEN_SYNC);
        edid_fprintf_boolean(stream, "Vsync Serrated", edid->in_flags & EDID_INPUT_F_VSYNC_SERRATED);
    }

    fprintf(stream, "\tHorizontal Size: %d cm\n", edid->h_size);
    fprintf(stream, "\tVertical Size: %d cm\n", edid->v_size);
    fprintf(stream, "\tGamma: %f\n", 1.0 + (float)edid->gamma/100);
    edid_fprintf_boolean(stream, "DPMS Standby", edid->f_flags & EDID_FEATURE_DPMS_STANDBY);
    edid_fprintf_boolean(stream, "DPMS Suspend", edid->f_flags & EDID_FEATURE_DPMS_SUSPEND);
    edid_fprintf_boolean(stream, "DPMS Active Off", edid->f_flags & EDID_FEATURE_DPMS_ACTIVE_OFF);
    if (edid->in_flags & EDID_INPUT_F_DIGITAL) {
        fprintf(stream, "\tDisplay Type: %s\n", enumval_name(edid_digital_types, edid->f_flags & EDID_FEATURE_TYPE, "Unknown"));
    }
    else {
        fprintf(stream, "\tDisplay Type: %s\n", enumval_name(edid_analog_types, edid->f_flags & EDID_FEATURE_TYPE, "Unknown"));
    }
    edid_fprintf_boolean(stream, "RGB", edid->f_flags & EDID_FEATURE_SRGB);
    edid_fprintf_boolean(stream, "Preferred Timing", edid->f_flags & EDID_FEATURE_PREFERRED_TIMING);
    edid_fprintf_boolean(stream, "Contiguous Timing", edid->f_flags & EDID_FEATURE_CONTIGUOUS_TIMING);
}

static void
edid_fprintf_std_timing(FILE *stream, uint16_t timing)
{
    unsigned int hres, vres, freq;
    if (timing == EDID_TIMING_UNUSED) {
        return;
    }
    hres = EDID_TIMING_XRES_GET(timing);
    freq = EDID_TIMING_FREQUENCY_GET(timing);
    switch (timing & EDID_TIMING_ASPECT) {
        case EDID_TIMING_ASPECT_16_10:
            vres = (hres * 10) / 16;
            break;
        case EDID_TIMING_ASPECT_4_3:
            vres = (hres * 3) / 4;
            break;
        case EDID_TIMING_ASPECT_5_4:
            vres = (hres * 4) / 5;
            break;
        case EDID_TIMING_ASPECT_16_9:
        default:
            vres = (hres * 9) / 16;
    }
    fprintf(stream, "\tTiming: %ux%u @ %uHz\n", hres, vres, freq);
}

#if 0
static void
edid_fprintf_detail(FILE *stream, const struct edid_detail *detail)
{

} /* edid_fprintf_detail */
#endif

void
edid_fprint(const void *data, FILE *stream)
{
    const struct edid_data *edid = data;
    const uint8_t pattern[] = EDID_PATTERN;
    int i;

    if (memcmp(pattern, edid->header, sizeof(edid->header)) != 0) {
        fprintf(stream, "Invalid EDID Header\n");
        return;
    }

    fprintf(stream, "Display EDID:\n");
    edid_fprintf_header(stream, edid);
    edid_fprintf_basic(stream, edid);
    for (i = 0; i < ARRAY_SIZE(edid->timing); i++) {
        edid_fprintf_std_timing(stream, htole16(edid->timing[i]));
    }
} /* edid_fprint */
