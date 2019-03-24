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
    struct video_segment *seg;
    unsigned long playlen = state->loopend - state->loopstart;
    unsigned long address = state->fpga->seq->region_start;

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

    /* Search the recording for the actual frame address. */
    seg = video_segment_lookup(&state->seglist, state->position, &address);
    if (seg) {
        overlay_update(state, seg);
    } else {
        address = state->fpga->seq->region_start;
    }
    state->fpga->display->frame_address = address;
    state->fpga->display->manual_sync = 1;
}

/* Wrapper to call dbus asynchronously from the main thread. */
static gboolean
playback_signal_segment(gpointer data)
{
    struct pipeline_state *state = data;
    dbus_signal_segment(state);
    return FALSE;
}

static int
playback_region_add(struct pipeline_state *state)
{
    struct video_segment *seg;

    /* Read the FIFO to extract the new region info. */
    uint32_t start = state->fpga->seq->md_fifo_read;
    uint32_t end = state->fpga->seq->md_fifo_read;
    uint32_t last = state->fpga->seq->md_fifo_read;

    /* Ignore recording events within the live display or calibration regions. */
    if (start == state->fpga->display->fpn_address) return 0;
    if (start == state->fpga->seq->live_addr[0]) return 0;
    if (start == state->fpga->seq->live_addr[1]) return 0;
    if (start == state->fpga->seq->live_addr[2]) return 0;

    /* Update the recording region info, and add a new segment. */
    state->seglist.rec_start = state->fpga->seq->region_start;
    state->seglist.rec_stop = state->fpga->seq->region_stop;
    state->seglist.framesz = state->fpga->seq->frame_size;
    seg = video_segment_add(&state->seglist, start, end, last);
    if (!seg) {
        return 0;
    }

    /* Capture some metadata too. */
    seg->metadata.exposure = state->fpga->sensor->int_time;
    seg->metadata.interval = state->fpga->sensor->frame_period;

    return 1;
}

/* Flush recording segment data. */
void
playback_region_flush(struct pipeline_state *state)
{
    video_segment_flush(&state->seglist);
    playback_set(state, 0, LIVE_MAX_FRAMERATE, 0);
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
    state->loopend = state->seglist.totalframes;

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
    if (state->loopend > state->seglist.totalframes) state->loopend = state->seglist.totalframes;

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

/* Frame GPIO callback events. */
static void
playback_fsync(struct pipeline_state *state, int fd)
{
    sigset_t sigset;
    char buf[2];

    /* Read the current state of the GPIO. */
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, sizeof(buf));

    /* Paused and live display mode: do nothing. */
    if ((state->mode == PIPELINE_MODE_PAUSE) || (state->mode == PIPELINE_MODE_LIVE) || (state->mode == PIPELINE_MODE_BLACKREF)) {
        return;
    }
    /* Playback mode: unblock the timer to allow the next frame */
    else if (state->mode == PIPELINE_MODE_PLAY) {
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

#define PLAYBACK_POLL_INTERVAL  100
#define PLAYBACK_POLL_WATCHDOG  (5000 / PLAYBACK_POLL_INTERVAL)

static int debug_watchdog_timeouts = 0;

/* Thread for managing the playback frames, this *MUST* run from a separate thread or
 * the GST/OMX elements may get stuck in a deadlock. */
static void *
playback_thread(void *arg)
{
    struct pipeline_state *state = (struct pipeline_state *)arg;
    struct pollfd pfd;
    sigset_t mask;
    int watchdog = PLAYBACK_POLL_WATCHDOG;
    int fsync_fd;
    int err;

    /* Open the frame sync GPIO. */
    /* TODO: Handle errors? */
    fsync_fd = ioport_open(state->iops, "frame-irq", O_RDONLY | O_NONBLOCK);

    /* Unblock our desired signals. */
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

    pfd.fd = fsync_fd;
    pfd.events = POLLPRI | POLLERR;
    pfd.revents = 0;
    while (1) {
        int newsegs = 0;
        err = poll(&pfd, 1, PLAYBACK_POLL_INTERVAL);
        if ((err == 0) && (state->mode >= PIPELINE_MODE_PLAY)) watchdog--;
        if (err < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (watchdog == 0) {
            fprintf(stderr, "Warning: Playback watchdog expired - retrying frame!\n");
            debug_watchdog_timeouts++;
            state->fpga->display->manual_sync = 1;
            watchdog = PLAYBACK_POLL_WATCHDOG;
        }

        /* Grab new regions from the recording sequencer. */
        while (!(state->fpga->seq->status & SEQ_STATUS_FIFO_EMPTY)) {
            newsegs += playback_region_add(state);
        }
        /* Emit a D-Bus signal from the main loop if we got new segments. */
        if (newsegs > 0) {
            GSource *segsource = g_idle_source_new();
            if (segsource) {
                g_source_set_callback(segsource, playback_signal_segment, state, NULL);
                g_source_attach(segsource, g_main_loop_get_context(state->mainloop));
            }
        }

        /* Read the current state of the GPIO. */
        if (pfd.revents) {
            playback_fsync(state, fsync_fd);
            watchdog = PLAYBACK_POLL_WATCHDOG;
        }
    }
}

void
playback_init(struct pipeline_state *state)
{
    struct sigevent sigev;
    struct sigaction sigact;
    pthread_t playback_handle;

    /*
     * TODO: Should be made private to the playback thread
     * or we risk pointer dereferencing bugs when updating
     * the playback regions.
     */
    video_segments_init(&state->seglist, 0, 0, state->fpga->seq->frame_size);

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
    pthread_create(&playback_handle, NULL, playback_thread, state);

    /* Start off in live display mode */
    state->mode = PIPELINE_MODE_LIVE;
    state->control = state->fpga->display->control & DISPLAY_CTL_COLOR_MODE;
    if (state->config.peaking) {
        state->control |= (DISPLAY_CTL_FOCUS_PEAK_ENABLE | state->config.peaking);
    }
    if (state->config.zebra) {
        state->control |= DISPLAY_CTL_ZEBRA_ENABLE;
    }
    
    /* Create the timer used for driving the playback state machine. */
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_value.sival_ptr = state;
    timer_create(CLOCK_MONOTONIC, &sigev, &state->playtimer);
}
