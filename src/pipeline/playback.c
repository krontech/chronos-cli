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
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>

#include "pipeline.h"

static void playback_rate_init(struct pipeline_state *state);
static void playback_rate_update(struct pipeline_state *state);

/*===============================================
 * Timer for Framerate Control and Watchdog
 *===============================================
 */
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

/* Disable the playback timer entirely. */
static void
playback_timer_disarm(struct pipeline_state *state)
{
    struct itimerspec ts = {
        .it_interval = {0, 0},
        .it_value = {0, 0},
    };
    timer_settime(state->playtimer, 0, &ts, NULL);
}

/* (Re)schedule the playback timer as a watchdog. */
static void
playback_timer_watchdog(struct pipeline_state *state)
{
    struct itimerspec ts = {
        .it_interval = {0,0},
        .it_value = {5, 0}
    };
    timer_settime(state->playtimer, 0, &ts, NULL);
}

/*===============================================
 * Frame Playback and Seeking
 *===============================================
 */
static void
playback_frame_advance(struct pipeline_state *state, int delta)
{
    struct playback_region *r;
    unsigned long count = 0;
    unsigned long playlen = state->loopend - state->loopstart;
    unsigned long segment;

    /* If no regions have been set, then just display the first address in memory. */
    if (!state->totalframes) {
        state->fpga->display->frame_address = state->fpga->seq->region_start;
        state->fpga->display->manual_sync = 1;
        return;
    }

    /* Advance the logical frame number. */
    if (delta > 0) {
        if (delta >= playlen) delta %= playlen;
        state->position += delta;
        if (state->position >= state->loopend) state->position -= playlen;
    }
    else if (delta < 0) {
        if (-delta >= playlen) delta %= playlen;
        if (state->position < (state->loopstart - delta)) state->position += playlen;
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
            overlay_update(state, r);
            break;
        }
        count += state->segsize;
    }

    /* Play the frame */
    state->fpga->display->manual_sync = 1;
}

/* Signal handler for the playback timer. */
static void
playback_signal(int signo, siginfo_t *info, void *ucontext)
{
    struct pipeline_state *state = cam_pipeline_state();
    sigset_t sigset;

    /* no-op unless we're in playback mode. */
    if (state->mode != PIPELINE_MODE_PLAY) {
        return;
    }
    playback_timer_rearm(state);
    switch (signo) {
        case SIGUSR1:
            if (info->si_code == SI_QUEUE) {
                playback_frame_advance(state, info->si_int);
            } else {
                playback_frame_advance(state, 1);
            }
            break;

        case SIGUSR2:
            if (info->si_code != SI_QUEUE) {
                state->position = 0;
            } else if (info->si_int >= 0) {
                state->position = info->si_int;
            } else {
                /* TODO: Handle special commands via negative numbers. */
                state->position = 0;
            }
            playback_frame_advance(state, 0);
            break;

        default:
            playback_frame_advance(state, state->playdelta);
            break;
    }
    
    /* Block delivery of our signal until the frame has been delivered. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);
}

/* Frame sync interrupt handler. */
static void
playback_fsync(struct pipeline_state *state)
{
    /* Paused and live display mode: do nothing. */
    if ((state->mode == PIPELINE_MODE_PAUSE) || (state->mode == PIPELINE_MODE_LIVE)) {
        return;
    }
    /* Playback mode: unblock the timer to allow the next frame */
    else if (state->mode == PIPELINE_MODE_PLAY) {
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGALRM);
        pthread_sigmask(SIG_UNBLOCK, &sigset, NULL);
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

/*===============================================
 * Recording Region Management
 *===============================================
 */
/* Test if two regions overlap each other.  */
static int
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

static void
playback_region_totals(struct pipeline_state *state)
{
    struct playback_region *r;
    unsigned long nsegs = 0;
    unsigned long nframes = 0;
    for (r = state->region_head; r; r = r->next) {
        nsegs++;
        nframes += (r->size / r->framesz);
    }
    state->totalframes = nframes;
    state->totalsegs = nsegs;
}

static int
playback_region_add(struct pipeline_state *state)
{
    struct playback_region *region;
    sigset_t sigset;
    /* Read the FIFO to extract the new region info. */
    uint32_t start = state->fpga->seq->md_fifo_read;
    uint32_t end = state->fpga->seq->md_fifo_read;
    uint32_t last = state->fpga->seq->md_fifo_read;

    /* Ignore recording events within the live display or calibration regions. */
    if (start == state->fpga->display->fpn_address) return 0;
    if (start == state->fpga->seq->live_addr[0]) return 0;
    if (start == state->fpga->seq->live_addr[1]) return 0;
    if (start == state->fpga->seq->live_addr[2]) return 0;

    /* Prepare memory for the new region. */
    region = malloc(sizeof(struct playback_region));
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
    playback_region_totals(state);
    return 1;
}

/* Flush recording segment data. */
void
playback_region_flush(struct pipeline_state *state)
{
    while (state->region_head) {
        playback_region_delete(state, state->region_head);
    }
    state->totalframes = 0;
    playback_play(state, 0, LIVE_MAX_FRAMERATE, 0);
}

/*===============================================
 * Playback State Changes
 *===============================================
 */
/* This would typically be called from outside the playback thread to change operation. */
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

        case PIPELINE_MODE_DNG:
            state->fpga->display->pipeline |= DISPLAY_PIPELINE_RAW_16PAD;
        case PIPELINE_MODE_TIFF_RAW:
        case PIPELINE_MODE_RAW16:
        case PIPELINE_MODE_RAW12:
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
    }
} /* playback_goto */

/* Switch to playback mode, with the desired position and playback rate. */
void
playback_play(struct pipeline_state *state, unsigned long frame, unsigned int rate, int mul)
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
    if (state->loopend > state->totalframes) state->loopend = state->totalframes;

    /* Set playback mode and re-arm the timer. */
    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
    state->fpga->display->control = control;
    playback_timer_rearm(state);
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

/*===============================================
 * Playback Module Threading
 *===============================================
 */
/* Wrapper to generate dbus signals from the main thread. */
static gboolean
playback_signal_segment(gpointer data)
{
    struct pipeline_state *state = data;
    dbus_signal_segment(state);
    return FALSE;
}

/*
 * Configure the frame sync GPIO for bi-directional edge detection, either
 * returns a correctly configured file descriptor, or less than zero on
 * error.
 */
static int
playback_setup_fsync(const char *value)
{
    FILE *fp;
    char path[PATH_MAX];
    int dirlen;

    /* Get the length of the directory name for the GPIO value. */
    dirlen = strlen(value);
    if (dirlen > sizeof(path)) {
        errno = EINVAL;
        return -1;
    }
    while ((dirlen > 0) && (value[dirlen] != '/')) dirlen--;

    /* Configure direction. */
    memcpy(path, value, dirlen);
    strcpy(path+dirlen, "direction");
    fp = fopen(path, "w");
    if (fp) {
        fputs("in", fp);
        fclose(fp);
    }

    /* Configure Edge detection */
    memcpy(path, value, dirlen);
    strcpy(path+dirlen, "edge");
    fp = fopen(path, "w");
    if (fp) {
        fputs("rising", fp);
        fclose(fp);
    }

    /* And finally, open the GPIO file descriptor */
    return open(value, O_RDONLY | O_NONBLOCK);
}

/* Thread for managing the playback frames, this *MUST* run from a separate thread or
 * the GST/OMX elements are likely to get stuck in a deadlock. */
static void *
playback_thread(void *arg)
{
    struct pipeline_state *state = (struct pipeline_state *)arg;
    struct pollfd pfd;
    sigset_t mask;
    int newsegs;
    int fsync;

    /* Open the frame sync GPIO or give up and fail. */
    fsync = playback_setup_fsync(ioport_find_by_name(state->iops, "frame-irq"));
    if (fsync < 0) {
        fprintf(stderr, "Failed to setup frame sync GPIO: %s\n", strerror(errno));
        return NULL;
    }

    /* Block the playback signals */
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    /* Wait for frame sync and check for new recording segments. */
    pfd.fd = fsync;
    pfd.events = POLLPRI | POLLERR;
    pfd.revents = 0;
    sigemptyset(&mask);
    while (1) {
        struct timespec ts = {0, 100 * 1000 * 1000};
        int ready = ppoll(&pfd, 1, &ts, &mask);
        if (ready < 0) {
            if (errno != EINTR) break;
            continue;
        }

        /* Check for new regions from the recording sequencer. */
        newsegs = 0;
        while (!(state->fpga->seq->status & SEQ_STATUS_FIFO_EMPTY)) {
            if (playback_region_add(state) > 0) newsegs++;
        }
        /* Emit a signal from the main loop if there were new segments.  */
        if (newsegs) {
            GSource *segsource = g_idle_source_new();
            if (segsource) {
                g_source_set_callback(segsource, playback_signal_segment, state, NULL);
                g_source_attach(segsource, g_main_loop_get_context(state->mainloop));
            }
        }

        /* Read the current state of the GPIO. */
        if (pfd.revents) {
            char buf[2];
            lseek(fsync, 0, SEEK_SET);
            read(fsync, buf, sizeof(buf));

            playback_fsync(state);
        }
    }

    /* Cleanup */
    playback_region_flush(state);
    close(fsync);
}

/*===============================================
 * Playback Module Setuo
 *===============================================
 */
void
playback_init(struct pipeline_state *state)
{
    struct sigevent sigev;
    struct sigaction sigact;

    /*
     * TODO: Should be made private to the playback thread
     * or we risk pointer dereferencing bugs when updating
     * the playback regions.
     */
    state->region_head = NULL;
    state->region_tail = NULL;

    /* Install the desired signal handlers. */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = playback_signal;
    sigaction(SIGALRM, &sigact, NULL);
    sigaction(SIGUSR1, &sigact, NULL);
    sigaction(SIGUSR2, &sigact, NULL);

    /* Mask off the signals so that only the playback thread will receive them. */
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGALRM);
    sigaddset(&sigact.sa_mask, SIGUSR1);
    sigaddset(&sigact.sa_mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &sigact.sa_mask, NULL);

    /* Start the playback thread. */
    pthread_create(&state->playthread, NULL, playback_thread, state);

    /* Start off in live display mode */
    state->mode = PIPELINE_MODE_LIVE;
    state->control = state->fpga->display->control & (DISPLAY_CTL_COLOR_MODE | DISPLAY_CTL_FOCUS_PEAK_COLOR);

    /* Create the timer used for driving the playback state machine. */
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_value.sival_ptr = state;
    timer_create(CLOCK_MONOTONIC, &sigev, &state->playtimer);
}
