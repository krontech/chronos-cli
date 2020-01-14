/****************************************************************************
 *  Copyright (C) 2017 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>

#include "pipeline.h"

void
overlay_clear(struct pipeline_state *state)
{
    state->fpga->overlay->control = 0;
    state->fpga->overlay->text0_xsize = 1; /* BUG: waiting for an FPGA fix. */
    state->fpga->overlay->text1_xsize = 1; /* BUG: waiting for an FPGA fix. */
}

void
overlay_setup(struct pipeline_state *state)
{
    if (!state->overlay.enable) {
        overlay_clear(state);
    }
    else {
        /* Default font sizing. */
        unsigned int fontsize = 25;
        unsigned int margin = 8;
        unsigned int height = state->overlay.height;
        if (!height) height = fontsize + (margin * 2);

        /* Compute the vertical size and offset. */
        state->fpga->overlay->text0_ysize = height;
        if (state->overlay.yoff > (state->source.vres - height)) {
            state->fpga->overlay->text0_ypos = (state->source.vres - height);
        } else {
            state->fpga->overlay->text0_ypos = state->overlay.yoff;
        }

        /* Compute the horizontal size and offset. */
        state->fpga->overlay->text0_xpos = state->overlay.xoff;
        if (state->overlay.width) {
            state->fpga->overlay->text0_xsize = state->overlay.width;
        } else {
            state->fpga->overlay->text0_xsize = (state->source.hres - state->overlay.xoff);
        }

        /* Text size, margin and colour. */
        state->fpga->overlay->text0_xoffset = margin;
        state->fpga->overlay->text0_yoffset = margin;

        /* Enable the text box. */
        state->fpga->overlay->control = OVERLAY_CONTROL_FONT_SIZE(fontsize) | OVERLAY_CONTROL_TEXTBOX0;
    }
}

static const char *
mkformat(char *output, const char *fmt, unsigned int fmtlen, const char *suffix)
{
    if ((fmtlen + strlen(suffix)) >= OVERLAY_TEXT_LENGTH) {
        return strcpy(output, "");
    } else {
        memcpy(output, fmt, fmtlen);
        return strcpy(output + fmtlen, suffix);
    }
}

static long long
triggertime_counts(struct pipeline_state *state, const struct video_segment *seg)
{
    long frames = seg->nframes - (state->position - seg->frameno) - 1;
    long delay = state->fpga->seq->trig_delay;
    return (long long)(delay - frames) * seg->metadata.interval;
}

static double
triggertime_float(struct pipeline_state *state, const struct video_segment *seg, char specifier)
{
    long long counts = triggertime_counts(state, seg);
    switch (specifier) {
        case 'U':
            return (double)counts / (double)(seg->metadata.timebase / 1000000);
        case 'M':
            return (double)counts / (double)(seg->metadata.timebase / 1000);
        case 'S':
        default:
            return (double)counts / (double)seg->metadata.timebase;
    }
}

void
overlay_update(struct pipeline_state *state, const struct video_segment *seg)
{
    char textbox[OVERLAY_TEXT_LENGTH];
    char tempfmt[OVERLAY_TEXT_LENGTH];
    const char *format = state->overlay.format;
    unsigned int len, maxLength;

    if (*format == '\0') {
        return;
    }
    
    len = 0;
    while (len < sizeof(textbox)) {
        /* Copy text until we encounter a format specifier. */
        const char *fmtstart;
        char specifier;
        if (*format != '%') {
            textbox[len++] = *format++;
            continue;
        }

        /* Sum up the length of the printf format specifier */
        fmtstart = format++;
        while (strchr("-+ #0", *format)) format++;  /* Count the flags */
        while (isdigit(*format)) format++;          /* Count the width */
        if (*format == '.') {
            format++;
            while (isdigit(*format)) format++;      /* Count the precision */
        }
        specifier = *(format++);                    /* Parse the specifier */

        /* Print out the format */
        switch (specifier) {
            /* Litteral percent */
            case '%':
                textbox[len++] = '%';
                break;
            
            /* Frame number */
            case 'f':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lu");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, state->position + 1);
                break;
            
            /* Total frames */
            case 't':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lu");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, state->seglist.totalframes);
                break;
            
            /* Segment number */
            case 'g':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lu");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, seg->segno + 1);
                break;
            
            /* Segment frame */
            case 'h':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lu");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, state->position - seg->frameno + 1);
                break;
            
            /* Total segments */
            case 'i':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lu");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, state->seglist.totalsegs);
                break;
            
            /* Segment size */
            case 'z':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lu");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, seg->nframes);
                break;

            /* Frame time from trigger as an integer number of: */
            case 'n':
                /* nanoseconds */
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "lld");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, triggertime_counts(state, seg) * 1000000000LL / seg->metadata.timebase);
                break;
            case 'u':
                /* microseconds */
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "ld");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, (long)((triggertime_counts(state, seg) * 1000000) / seg->metadata.timebase));
                break;
            case 'm':
                /* milliseconds */
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "ld");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, (long)((triggertime_counts(state, seg) * 1000) / seg->metadata.timebase));
                break;

            /* Frame time from trigger as a floating point number of: */
            case 'U': /* microseconds */
            case 'M': /* milliseconds */
            case 'S': /* seconds */
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "f");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, triggertime_float(state, seg, specifier));
                break;

            /* Exposure time (floating point) */
            case 'e':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "f");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, (double)seg->metadata.exposure / 100.0);
                break;
            /* Framerate (floating point) */
            case 'r':
                mkformat(tempfmt, fmtstart, (format - fmtstart - 1), "f");
                len += snprintf(textbox + len, sizeof(textbox) - len, tempfmt, (double)seg->metadata.timebase / seg->metadata.interval);
                break;
        }
    }

    /* Write the text and enable. */
    if (len > sizeof(textbox)) len = sizeof(textbox);

    /* Ensure the text isn't too long for the width of the image. */
    maxLength = state->source.hres / 16;
    if(len > maxLength){
        textbox[maxLength] = '\0';
    }

    strncpy((char *)state->fpga->overlay->text0_buffer, textbox, len);
}
