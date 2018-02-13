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
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#include "pipeline.h"

/* Returns the number of frames to add per timer tick. */
static inline unsigned int
playback_divisor(int rate)
{
    return (abs(rate) + LIVE_MAX_FRAMERATE - 1) / LIVE_MAX_FRAMERATE;
}

/*
 * Re-arm the playback timer in single-shot mode. By blocking the delivery
 * of SIGARLM, this allows clock stretching to keep time with the FPGA when
 * we approach the live framerate limits.
 */
static void
playback_timer_rearm(struct pipeline_state *state)
{
    if (state->playrate == 0) {
        /* Pause the video by playing the same frame at 60Hz */
        struct itimerspec ts = {
            .it_interval = {0, 0},
            .it_value = {0, 1000000000 / LIVE_MAX_FRAMERATE},
        };
        timer_settime(state->playtimer, 0, &ts, NULL);
    }
    /* Start the playback frame timer. */
    else {
        unsigned int divisor = playback_divisor(state->playrate);
        unsigned long nsec = ((1000000000ULL * divisor) + (abs(state->playrate) - 1)) / abs(state->playrate);
        struct itimerspec ts = {
            .it_interval = {0, 0},
            .it_value = {0, nsec},
        };
        timer_settime(state->playtimer, 0, &ts, NULL);
    }
}


/* Signal handler for the playback timer. */
static void
playback_signal(int signo)
{
    struct pipeline_state *state = cam_pipeline_state();
    unsigned int divisor = playback_divisor(state->playrate);
    struct playback_region *r;
    unsigned long count = 0;

    /* no-op if we're in live display mode. */
    if (!(state->fpga->display->control & DISPLAY_CTL_ADDRESS_SELECT)) return;
    playback_timer_rearm(state);

    /* If no regions have been set, then just display the first address in memory. */
    if (!state->totalframes) {
        state->fpga->display->frame_address = state->fpga->seq->region_start;
        state->fpga->display->manual_sync = 1;
        return;
    }

    /* Advance the logical frame number. */
    if (state->playrate > 0) {
        state->lastframe = (state->lastframe + divisor) % state->totalframes;
    }
    if (state->playrate < 0) {
        if (state->lastframe < divisor) state->lastframe += state->totalframes;
        state->lastframe -= divisor;
    }

    /* Search the recording regions for the actual frame address. */
    count = 0;
    for (r = state->region_head; r; r = r->next) {
        unsigned long nframes = (r->size / r->framesz);
        if ((count + nframes) > state->lastframe) {
            unsigned long frameoff = r->offset / r->framesz;
            unsigned long relframe = (state->lastframe - count + frameoff) % nframes;
            state->fpga->display->frame_address = r->base + (relframe * r->framesz);
            break;
        }
        count += nframes;
    }
    state->fpga->display->manual_sync = 1;

    /* If the frame sync GPIO is available, block delivery of our signal until the
     * frame has been delivered. */
    if (state->fsync_fd >= 0) {
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGALRM);
        sigprocmask(SIG_BLOCK, &sigset, NULL);
    }
}

/* Test if two regions overlap each other.  */
int
playback_region_overlap(struct playback_region *a, struct playback_region *b)
{
    /* Sort a and b. */
    if (a->base > b->base) {struct playback_region *tmp = a; a = b; b = tmp;}
    return ((a->base + a->size) > b->base);
}

static void
playback_region_delete(struct pipeline_state *state, struct playback_region *r)
{
    if (r->next) r->next->prev = r->prev;
    else state->region_tail = r->prev;
    if (r->prev) r->prev->next = r->next;
    else state->region_head = r->next;
    free(r);
}

static unsigned long
playback_region_total(struct pipeline_state *state)
{
    struct playback_region *r;
    unsigned long nframes = 0;
    for (r = state->region_head; r; r = r->next) {
        nframes += (r->size / r->framesz);
    }
    return nframes;
}

void
playback_lock(sigset_t *prev)
{
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    sigprocmask(SIG_BLOCK, &block, prev);
}

void
playback_unlock(sigset_t *prev)
{
    sigprocmask(SIG_BLOCK, prev, NULL);
}

int
playback_region_add(struct pipeline_state *state, unsigned long base, unsigned long size, unsigned long offset)
{
    sigset_t sigset;

    /* Prepare memory for the new region. */
    struct playback_region *region = malloc(sizeof(struct playback_region));
    if (!region) {
        return -1;
    }
    region->base = base;
    region->size = size;
    region->offset = offset;
    region->framesz = state->fpga->seq->frame_size;

    playback_lock(&sigset);

    /* Free any regions that would overlap. */
    while (state->region_head) {
        if (!playback_region_overlap(region, state->region_head)) break;
        playback_region_delete(state, state->region_head);
    }

    /* Link this region into the end of the list. */
    region->next = NULL;
    region->prev = state->region_tail;
    if (region->prev) {
        region->prev->next = region;
        state->region_tail = region;
    }
    else {
        state->region_tail = region;
        state->region_head = region;
    }

    /* Update the total recording region size and reset back to the start. */
    state->totalframes = playback_region_total(state);
    playback_set(state, 0, 0);
    playback_unlock(&sigset);
    return 0;
}

void
playback_region_flush(struct pipeline_state *state)
{
    sigset_t sigset;
    playback_lock(&sigset);
    while (state->region_head) {
        playback_region_delete(state, state->region_head);
    }
    state->totalframes = 0;
    playback_set(state, 0, 0);
    playback_unlock(&sigset);
}

void
playback_set(struct pipeline_state *state, unsigned long frame, int rate)
{
    state->playrate = rate;
    state->lastframe = frame;
    playback_timer_rearm(state);
}

/* frame GPIO callback events. */
static gboolean
playback_fsync_callback(GIOChannel *source, GIOCondition cond, gpointer data)
{
    struct pipeline_state *state = (struct pipeline_state *)data;
    sigset_t sigset;
    char buf[2];

    /* Read the current state of the GPIO. */
    lseek(state->fsync_fd, 0, SEEK_SET);
    read(state->fsync_fd, buf, sizeof(buf));

    /* Unblock the playback frame timer. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    return FALSE;
}

void
playback_init(struct pipeline_state *state)
{
    struct sigevent sigev;
    state->region_head = NULL;
    state->region_tail = NULL;

    /* Open the frame sync GPIO. */
    state->fsync_fd = ioport_open(state->iops, "frame-irq", O_RDONLY | O_NONBLOCK);
    if (state->fsync_fd >= 0) {
        GIOChannel *channel = g_io_channel_unix_new(state->fsync_fd);
        g_io_add_watch(channel, G_IO_PRI | G_IO_ERR, (GIOFunc)playback_fsync_callback, state);       
    }

    /* Seek to the next frame in playback mode on SIGALRM */
    signal(SIGALRM, playback_signal);

    /* Create the timer used for driving the playback state machine. */
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_value.sival_ptr = state;
    timer_create(CLOCK_MONOTONIC, &sigev, &state->playtimer);

    playback_set(state, 0, 0);
}
