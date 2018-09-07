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
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>

#include "pipeline.h"

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
        struct itimerspec ts = {
            .it_interval = {0, 0},
            .it_value = {0, (1000000000ULL + state->playrate - 1) / state->playrate},
        };
        timer_settime(state->playtimer, 0, &ts, NULL);
    }
}

static void
playback_timer_disarm(struct pipeline_state *state)
{
    struct itimerspec ts = {
        .it_interval = {0, 0},
        .it_value = {0, 0},
    };
    timer_settime(state->playtimer, 0, &ts, NULL);
}

static void
playback_frame_advance(struct pipeline_state *state, int delta)
{
    struct playback_region *r;
    unsigned long count = 0;
    unsigned long segment;

    /* If no regions have been set, then just display the first address in memory. */
    if (!state->totalframes) {
        state->fpga->display->frame_address = state->fpga->seq->region_start;
        state->fpga->display->manual_sync = 1;
        return;
    }

    /* Advance the logical frame number. */
    if (delta > 0) {
        state->position += delta;
        if (state->position >= state->loopend) state->position -= (state->loopend - state->loopstart);
    }
    else if (delta < 0) {
        if (state->position < (state->loopstart - delta)) state->position += (state->loopend - state->loopstart);
        state->position += delta;
    }

    /* Search the recording regions for the actual frame address. */
    count = 0;
    state->segment = 0;
    for (r = state->region_head; r; r = r->next) {
        state->segment++;
        state->segframe = (state->position - count);
        state->segsize = (r->size / r->framesz);
        if (state->segframe < state->segsize) {
            unsigned long relframe = (state->segframe + (r->offset / r->framesz)) % state->segsize;
            state->fpga->display->frame_address = r->base + (relframe * r->framesz);
            break;
        }
        count += state->segsize;
    }
    overlay_update(state, r);
    state->fpga->display->manual_sync = 1;
}

/* Signal handler for the playback timer. */
static void
playback_signal(int signo)
{
    struct pipeline_state *state = cam_pipeline_state();

    /* no-op unless we're in playback mode. */
    if (state->mode != PIPELINE_MODE_PLAY) {
        return;
    }
    playback_timer_rearm(state);
    playback_frame_advance(state, state->playdelta);

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

static int
playback_region_add(struct pipeline_state *state)
{
    sigset_t sigset;
    /* Read the FIFO to extract the new region info. */
    uint32_t start = state->fpga->seq->md_fifo_read;
    uint32_t end = state->fpga->seq->md_fifo_read;
    uint32_t last = state->fpga->seq->md_fifo_read;

    /* Prepare memory for the new region. */
    struct playback_region *region = malloc(sizeof(struct playback_region));
    if (!region) {
        return -1;
    }
    region->framesz = state->fpga->seq->frame_size;
    region->base = start;
    region->size = (end - start) + region->framesz;
    region->offset = (last >= end) ? 0 : (last - start + region->framesz);
    /* Capture some metadata too */
    region->exposure = state->fpga->sensor->int_time;
    region->interval = state->fpga->sensor->frame_period;

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
    state->position = 0;
    state->totalframes = playback_region_total(state);
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
    playback_set(state, 0, LIVE_MAX_FRAMERATE, 0);
    playback_unlock(&sigset);
}

void
playback_goto(struct pipeline_state *state, unsigned int mode)
{
    uint32_t control = state->control;

    switch (mode) {
        case PIPELINE_MODE_LIVE:
            state->mode = PIPELINE_MODE_LIVE;
            state->playrate = 0;
            state->position = 0;
            overlay_clear(state);
            playback_timer_disarm(state);

            /* Clear sync inhibit to enable automatic video output. */
            control &= ~(DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
            state->fpga->display->control = control;
            break;
        
        case PIPELINE_MODE_PLAY:
            state->mode = PIPELINE_MODE_PLAY;
            state->playrate = LIVE_MAX_FRAMERATE;
            state->playdelta = 0;
            state->position = 0;

            /* Set playback mode and re-arm the timer. */
            control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
            control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
            state->fpga->display->control = control;
            overlay_setup(state);
            playback_timer_rearm(state);
            break;
        
        case PIPELINE_MODE_PAUSE:
            state->mode = PIPELINE_MODE_PAUSE;
            overlay_setup(state);
            playback_timer_disarm(state);
            control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
            control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
            state->fpga->display->control = control;
            state->fpga->display->pipeline &= ~(DISPLAY_PIPELINE_TEST_PATTERN | DISPLAY_PIPELINE_RAW_MODES | DISPLAY_PIPELINE_BYPASS_FPN);
            break;

        case PIPELINE_MODE_RAW12:
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_12BPP;
            state->mode = mode;
            state->preroll = 3;

            /* Enable the test pattern and begin prerolling */
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_TEST_PATTERN;
            control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
            control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
            state->fpga->display->control = control;
            state->fpga->display->manual_sync = 1;
            break;

        case PIPELINE_MODE_DNG:
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_16PAD;
        case PIPELINE_MODE_RAW16:
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_16BPP;
        case PIPELINE_MODE_TIFF:
        case PIPELINE_MODE_H264:
            state->mode = mode;
            state->preroll = 3;

            /* Enable the test pattern and begin prerolling */
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_TEST_PATTERN;
            control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
            control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
            state->fpga->display->control = control;
            state->fpga->display->manual_sync = 1;
            break;
        
        case PIPELINE_MODE_BLACKREF:
            state->mode = PIPELINE_MODE_BLACKREF;
            state->playrate = 0;
            state->position = 0;
            playback_timer_disarm(state);

            /* Clear sync inhibit to enable automatic video output. */
            /* Enable black cal mode to bybass calibration. */
            state->fpga->display->control = DISPLAY_CTL_BLACK_CAL_MODE;
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_BYPASS_FPN | DISPLAY_PIPELINE_RAW_16BPP | DISPLAY_PIPELINE_RAW_16PAD;
            break;
    }
} /* playback_goto */

/* Switch to playback mode, with the desired position and playback rate. */
void
playback_set(struct pipeline_state *state, unsigned long frame, unsigned int rate, int mul)
{
    uint32_t control = state->fpga->display->control;

    state->mode = PIPELINE_MODE_PLAY;
    state->playdelta = mul;
    state->playrate = rate;
    state->position = frame;
    state->loopstart = 0;
    state->loopend = state->totalframes;

    /* Set playback mode and re-arm the timer. */
    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
    state->fpga->display->control = control;
    overlay_setup(state);
    playback_timer_rearm(state);
}

/* Start playback at a given framerate and loop over a subset of frames. */
void
playback_loop(struct pipeline_state *state, unsigned long start, unsigned int rate, int mul, unsigned long count)
{
    uint32_t control = state->fpga->display->control;

    state->mode = PIPELINE_MODE_PLAY;
    state->playdelta = mul;
    state->playrate = rate;
    state->position = start;
    state->loopstart = start;
    state->loopend = start + count;

    /* Set playback mode and re-arm the timer. */
    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
    state->fpga->display->control = control;
    playback_timer_rearm(state);
}

/* Update the estimated frame rate with another frame interval time. */
static void
playback_rate_update(struct pipeline_state *state)
{
    struct timespec prev;
    long usec;
    
    /* Calculate how much time elapsed from the last frame. */
    memcpy(&prev, &state->frametime, sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, &state->frametime);
    usec = (state->frametime.tv_sec - prev.tv_sec) * 1000000;
    usec += (state->frametime.tv_nsec - prev.tv_nsec) / 1000;
    if (usec < 0) usec = 0;

    /* Update the framerate estimate. */
    state->frameisum -= state->frameival[state->frameidx];
    state->frameisum += usec;
    state->frameival[state->frameidx] = usec;
    state->frameidx = (state->frameidx + 1) % FRAMERATE_IVAL_BUCKETS;
}

/* Initialize the estimated frame rate. */
static void
playback_rate_init(struct pipeline_state *state)
{
    int i;
    const unsigned long usec = 1000000 / LIVE_MAX_FRAMERATE;

    clock_gettime(CLOCK_MONOTONIC, &state->frametime);
    state->frameidx = 0;
    state->frameisum = 0;
    for (i = 0; i < FRAMERATE_IVAL_BUCKETS; i++) {
        state->frameival[i] = usec;
        state->frameisum += usec;
    }
}

/* frame GPIO callback events. */
static void
playback_fsync_callback(struct pipeline_state *state)
{
    sigset_t sigset;
    char buf[2];

    /* Read the current state of the GPIO. */
    lseek(state->fsync_fd, 0, SEEK_SET);
    read(state->fsync_fd, buf, sizeof(buf));

    /* Paused and live display mode: do nothing. */
    if ((state->mode == PIPELINE_MODE_PAUSE) || (state->mode == PIPELINE_MODE_LIVE) || (state->mode == PIPELINE_MODE_BLACKREF)) {
        return;
    }
    /* Playback mode: unblock the timer to allow the next frame */
    else if (state->mode == PIPELINE_MODE_PLAY) {
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGALRM);
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    }
    /* Recording Modes: Preroll and then begin playback. */
    else if (state->preroll) {
        state->fpga->display->manual_sync = 1;
        fprintf(stderr, "Prerolling... %d\n", state->preroll);
        state->preroll--;
        if (!state->preroll) {
            state->buflevel = 10;
            state->fpga->display->pipeline &= ~DISPLAY_PIPELINE_TEST_PATTERN;
            playback_rate_init(state);
            g_object_get(G_OBJECT(state->source), "buffer-level", &state->buflevel, NULL);
        }
    }
    /* If the buffer level was high - play the next frame immediately. */
    /* This tends to improve throughput by pipelining the OMX round trips. */
    else if (state->buflevel > 2) {
        playback_rate_update(state);
        playback_frame_advance(state, 1);
        if (state->source) {
            g_object_get(G_OBJECT(state->source), "buffer-level", &state->buflevel, NULL);
        }
    }
    /* Otherwise, wait for more buffers to become available and then play the next frame. */
    else {
        /* Wait for flow control issues to clear. */
        while (state->source) {
            state->buflevel = 10;
            g_object_get(G_OBJECT(state->source), "buffer-level", &state->buflevel, NULL);
            if (state->buflevel > 1) break;
            usleep(10000);
        }
        playback_rate_update(state);
        playback_frame_advance(state, 1);
    }
    return;
}

/* Thread for managing the playback frames, this *MUST* run from a separate thread or
 * the GST/OMX elements may get stuck in a deadlock. */
static void *
playback_thread(void *arg)
{
    struct pipeline_state *state = (struct pipeline_state *)arg;
    struct pollfd pfd;
    const int msec = 100;
    int err;

    pfd.fd = state->fsync_fd;
    pfd.events = POLLPRI | POLLERR;
    pfd.revents = 0;
    while (1) {
        err = poll(&pfd, 1, -1);
        if (err < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Grab new regions from the recording sequencer. */
        while (!(state->fpga->seq->status & SEQ_STATUS_FIFO_EMPTY)) {
            playback_region_add(state);
        }

        /* Read the current state of the GPIO. */
        if (err > 0) {
            playback_fsync_callback(state);
        }
    } while(1);
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
        pthread_t playback_handle;
        pthread_create(&playback_handle, NULL, playback_thread, state);
    }

    /* Start off in live display mode */
    state->mode = PIPELINE_MODE_LIVE;
    state->control = state->fpga->display->control & (DISPLAY_CTL_COLOR_MODE | DISPLAY_CTL_FOCUS_PEAK_COLOR);

    /* Seek to the next frame in playback mode on SIGALRM */
    signal(SIGALRM, playback_signal);

    /* Create the timer used for driving the playback state machine. */
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_value.sival_ptr = state;
    timer_create(CLOCK_MONOTONIC, &sigev, &state->playtimer);
}
