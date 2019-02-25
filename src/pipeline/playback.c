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

/* Some special commands that can be passed through the pipe. */
#define PLAYBACK_PIPE_EXIT      (INT_MIN + 0)   /* Terminate and cleanup the playback thread. */
#define PLAYBACK_PIPE_LIVE      (INT_MIN + 1)   /* Seek to the live display stream. */
#define PLAYBACK_PIPE_FLUSH     (INT_MIN + 2)   /* Dump all recording segments from memory. */
#define PLAYBACK_PIPE_PREROLL   (INT_MIN + 3)   /* Begin prerolling video to start playback. */

#define PLAYBACK_POLL_INTERVAL 100
#define PLAYBACK_WATCHDOG_COUNT (5000 / PLAYBACK_POLL_INTERVAL)

static int playback_pipe = -1;
static void playback_rate_init(struct pipeline_state *state);
static void playback_rate_update(struct pipeline_state *state);

/*===============================================
 * Video Port Configuration
 *===============================================
 */
static void
playback_setup_timing(struct pipeline_state *state, unsigned int maxfps)
{
    const unsigned int hSync = 1;
    const unsigned int hBackPorch = 64;
    const unsigned int hFrontPorch = 4;
    const unsigned int vSync = 1;
    const unsigned int vBackPorch = 4;
    const unsigned int vFrontPorch = 1;
    unsigned int pxClock = 100000000;
    unsigned int minHPeriod;
    unsigned int hPeriod;
    unsigned int vPeriod;
    unsigned int fps;

    /* Inhibit frame sync while adjusting the display timing. */
    uint32_t control = state->fpga->display->control;
    state->fpga->display->control = (control | DISPLAY_CTL_SYNC_INHIBIT);

    /* FPGA version 3.14 and higher use a 133MHz pixel clock. */
    if (state->fpga->config->version > 3) {
        pxClock = 133333333;
    }
    else if ((state->fpga->config->version == 3) && (state->fpga->config->subver >= 14)) {
        pxClock = 133333333;
    }

    /* Force a delay to ensure that frame readout completes. */
    hPeriod = PIPELINE_MAX_HRES + hBackPorch + hSync + hFrontPorch;
    vPeriod = PIPELINE_MAX_VRES + vBackPorch + vSync + vFrontPorch;
    usleep(((unsigned long long)(vPeriod * hPeriod) * 1000000ULL) / pxClock);

    /* Calculate minimum hPeriod to fit within 2048 max vertical resolution. */
    hPeriod = hSync + hBackPorch + state->hres + hFrontPorch;
    minHPeriod = (pxClock / ((2048+vBackPorch+vSync+vFrontPorch) * maxfps)) + 1;
    if (hPeriod < minHPeriod) hPeriod = minHPeriod;

    /* Calculate the vPeriod and make sure it's large enough for the frame. */
    vPeriod = pxClock / (hPeriod * maxfps);
    if (vPeriod < (state->vres + vBackPorch + vSync + vFrontPorch)) {
        vPeriod = (state->vres + vBackPorch + vSync + vFrontPorch);
    }

    /* Calculate the FPS for debug output */
    fps = pxClock / (vPeriod * hPeriod);
    fprintf(stderr, "Setup display timing: %d*%d@%d (%lu*%lu max: %u)\n",
           (hPeriod - hBackPorch - hSync - hFrontPorch),
           (vPeriod - vBackPorch - vSync - vFrontPorch),
           fps, state->hres, state->vres, maxfps);

    /* Configure the FPGA */
    state->fpga->display->h_res = state->hres;
    state->fpga->display->h_out_res = state->hres;
    state->fpga->display->v_res = state->vres;
    state->fpga->display->v_out_res = state->vres;

    state->fpga->display->h_period = hPeriod;
    state->fpga->display->h_sync_len = hSync;
    state->fpga->display->h_back_porch = hBackPorch;

    state->fpga->display->v_period = vPeriod;
    state->fpga->display->v_sync_len = vSync;
    state->fpga->display->v_back_porch = vBackPorch;

    /* Release sync inhibit after reconfiguration. */
    state->fpga->display->control = control;
}

/*===============================================
 * Frame Playback and Seeking
 *===============================================
 */
static void
playback_frame_seek(struct pipeline_state *state, int delta)
{
    unsigned long playlen = state->loopend - state->loopstart;

    /* Do a no-op instead of dividing by zero. */
    if ((state->position < 0) || (playlen == 0)) {
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
}

static void
playback_frame_render(struct pipeline_state *state)
{
    struct playback_region *r;
    unsigned long count = 0;

    /* If no regions have been set, then just display the first address in memory. */
    if (!state->totalframes) {
        state->fpga->display->frame_address = state->fpga->seq->region_start;
        state->fpga->display->manual_sync = 1;
        return;
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

    /* no-op unless we're in playback and the seek pipe exists. */
    if ((playback_pipe < 0) || (state->mode != PIPELINE_MODE_PLAY)) {
        return;
    }
    switch (signo) {
        case SIGUSR1:
            /* Relative seek on SIGUSR1 */
            if (info->si_code == SI_QUEUE) {
                write(playback_pipe, &info->si_int, sizeof(int));
            } else {
                int delta = 1;
                write(playback_pipe, &delta, sizeof(int));
            }
            break;

        case SIGUSR2:
            /* Absolute seek on SIGUSR2. */
            if (info->si_code != SI_QUEUE) {
                int delta = 0;
                state->playrate = 0;
                state->position = 0;
                write(playback_pipe, &delta, sizeof(int));
            } else if (info->si_int >= 0) {
                int delta = 0;
                state->position = info->si_int;
                state->playrate = 0;
                write(playback_pipe, &delta, sizeof(int));
            } else {
                int command = PLAYBACK_PIPE_LIVE;
                write(playback_pipe, &command, sizeof(int));
            }
            break;
    }
}

/* Frame sync interrupt handler. */
static void
playback_fsync(struct pipeline_state *state)
{
    /* Paused: do nothing. */
    if (state->mode == PIPELINE_MODE_PAUSE) {
        return;
    }
    /* Playback mode: Render the next frame. */
    else if (state->mode == PIPELINE_MODE_PLAY) {
        /* Do a quickly delta-sigma to set the framerate. */
        int nextcount = state->playcounter + state->playrate;
        state->playcounter = nextcount % LIVE_MAX_FRAMERATE;
        playback_frame_seek(state, nextcount / LIVE_MAX_FRAMERATE);
        playback_frame_render(state);
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
        playback_frame_seek(state, 1);
        playback_frame_render(state);
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
        playback_frame_seek(state, 1);
        playback_frame_render(state);
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
    region->hres = state->hres;
    region->vres = state->vres;
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

/*===============================================
 * Playback State Changes
 *===============================================
 */
void
playback_pause(struct pipeline_state *state)
{
    uint32_t control = state->control;

    fprintf(stderr, "Pausing playback\n");
    
    state->mode = PIPELINE_MODE_PAUSE;
    overlay_setup(state);
    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
    state->fpga->display->control = control;
    state->fpga->display->pipeline &= ~(DISPLAY_PIPELINE_TEST_PATTERN | DISPLAY_PIPELINE_RAW_MODES | DISPLAY_PIPELINE_BYPASS_FPN);
}

/* This would typically be called from outside the playback thread to change operation. */
void
playback_preroll(struct pipeline_state *state, unsigned int mode)
{
    int command = PLAYBACK_PIPE_PREROLL;
    fprintf(stderr, "Prerolling playback (mode=%d)\n", mode);
    state->mode = mode;
    write(playback_pipe, &command, sizeof(command));
} /* playback_preroll */

/* Switch to live display mode. */
void
playback_live(struct pipeline_state *state)
{
    int command = PLAYBACK_PIPE_LIVE;
    write(playback_pipe, &command, sizeof(command));
}

/* Switch to playback mode, with the desired position and playback rate. */
void
playback_play(struct pipeline_state *state, unsigned long frame, int framerate)
{
    int delta = 0;

    /* Update the frame position and then seek zero frames forward. */
    state->playrate = framerate;
    state->playcounter = 0;
    state->loopstart = 0;
    state->loopend = state->totalframes;
    state->position = frame;
    write(playback_pipe, &delta, sizeof(delta));
}

/* Start playback at a given framerate and loop over a subset of frames. */
void
playback_loop(struct pipeline_state *state, unsigned long start, int framerate, unsigned long count)
{
    int delta = 0;

    /* Update the frame position and then seek zero frames forward. */
    state->playrate = framerate;
    state->playcounter = 0;
    state->loopstart = start;
    state->loopend = start + count;
    if (state->loopend > state->totalframes) state->loopend = state->totalframes;
    state->position = start;
    write(playback_pipe, &delta, sizeof(delta));
}

void
playback_flush(struct pipeline_state *state)
{
    int command = PLAYBACK_PIPE_FLUSH;
    write(playback_pipe, &command, sizeof(command));
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
    int watchdog = PLAYBACK_WATCHDOG_COUNT;

    int seekfds[2];
    struct pipeline_state *state = (struct pipeline_state *)arg;
    struct pollfd pfd[2];
    sigset_t mask;
    int newsegs;
    int flags;
    int fsync;

    /* Open the frame sync GPIO or give up and fail. */
    fsync = playback_setup_fsync(ioport_find_by_name(state->iops, "frame-irq"));
    if (fsync < 0) {
        fprintf(stderr, "Failed to setup frame sync GPIO: %s\n", strerror(errno));
        return NULL;
    }

    /* Setup a pipe for playback commands and seeking. */
    if (pipe(seekfds) != 0) {
        fprintf(stderr, "Failed to create playback pipe: %s\n", strerror(errno));
        close(fsync);
        return NULL;
    }
    flags = fcntl(seekfds[0], F_GETFL);
    fcntl(seekfds[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(seekfds[1], F_GETFL);
    fcntl(seekfds[1], F_SETFL, flags | O_NONBLOCK);
    playback_pipe = seekfds[1];

    /* Unblock the playback signals for our thread.*/
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

    /* Wait for frame sync when rendering frames. */
    pfd[0].fd = fsync;
    pfd[0].events = POLLPRI | POLLERR;
    pfd[0].revents = 0;

    /* Wait for seek events to navigate through recordings. */
    pfd[1].fd = seekfds[0];
    pfd[1].events = POLLIN | POLLERR;
    pfd[1].revents = 0;

    while (1) {
        int ready = poll(pfd, 2, PLAYBACK_POLL_INTERVAL);
        if (ready == 0) {
            watchdog--;
        }
        else if (ready < 0) {
            if (errno != EINTR) break;
            continue;
        }

        /*===============================================
         * Check for New Recording Regions. 
         *===============================================
         */
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

        /*===============================================
         * Handle Playback Commands
         *===============================================
         */
        if (pfd[1].revents & POLLIN) {
            uint32_t control = state->control;
            int delta = 0;
            read(seekfds[0], &delta, sizeof(delta));

            if (delta == PLAYBACK_PIPE_EXIT) {
                /* Terminate and cleanup. */
                break;
            }
            else if (delta == PLAYBACK_PIPE_PREROLL) {
                /* Setup prerolling for filesaves. */
                if (PIPELINE_IS_SAVING(state->mode)) {
                    state->preroll = 3;
                    state->fpga->display->pipeline |= DISPLAY_PIPELINE_TEST_PATTERN;
                    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
                    state->fpga->display->control = control;

                    playback_setup_timing(state, SAVE_MAX_FRAMERATE);
                    overlay_setup(state);
                }
                /* Configure the overlay and display timing. */
                else if (state->mode == PIPELINE_MODE_PLAY) {
                    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
                    state->fpga->display->control = control;
                    playback_setup_timing(state, LIVE_MAX_FRAMERATE);

                    state->playcounter = 0;
                    if (state->position < 0) {
                        overlay_clear(state);
                        control &= ~(DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                        state->fpga->display->control = control;
                    }
                    else {
                        overlay_setup(state);
                    }
                }

                /* Start playback by faking a fsync edge. */
                pfd[0].revents |= POLLPRI;
            }
            else if (delta == PLAYBACK_PIPE_FLUSH) {
                /* Drop all recording segments. */
                while (state->region_head) {
                    playback_region_delete(state, state->region_head);
                }
                state->totalframes = 0;
            }
            else if (delta == PLAYBACK_PIPE_LIVE) {
                /* Clear address select and sync inhibit to enter live display mode. */
                uint32_t control = state->control;
                overlay_clear(state);
                control &= ~(DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                state->fpga->display->control = control;

                /* Set the playback position as negative to indicate we're in live. */
                state->playrate = 0;
                state->position = -1;
            }
            else {
                /* Otherwise, seek through recorded video. */
                control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
                state->fpga->display->control = control;
                overlay_setup(state);
                playback_frame_seek(state, delta);
            }
        }

        /*===============================================
         * Render Frames on Frame Sync Rising Edge
         *===============================================
         */
        if (pfd[0].revents) {
            char buf[2];
            lseek(fsync, 0, SEEK_SET);
            read(fsync, buf, sizeof(buf));

            /* Reset the watchdog on fsync. */
            watchdog = PLAYBACK_WATCHDOG_COUNT;

            playback_fsync(state);
        }
        else if (watchdog <= 0) {
            /* We went too long without receiving a frame. */
            if (state->mode != PIPELINE_MODE_PAUSE) {
                fprintf(stderr, "Warning: Playback watchdog expired - retrying frame!\n");
                state->fpga->display->manual_sync;
            }
            watchdog = PLAYBACK_WATCHDOG_COUNT;
        }
    }

    /* Cleanup */
    while (state->region_head) {
        playback_region_delete(state, state->region_head);
    }
    close(seekfds[0]);
    close(seekfds[1]);
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
    sigaction(SIGUSR1, &sigact, NULL);
    sigaction(SIGUSR2, &sigact, NULL);

    /* Mask off the signals so that only the playback thread will receive them. */
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGUSR1);
    sigaddset(&sigact.sa_mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &sigact.sa_mask, NULL);

    /* Start the playback thread. */
    pthread_create(&state->playthread, NULL, playback_thread, state);

    /* Start off paused. */
    state->mode = PIPELINE_MODE_PAUSE;
    state->control = (state->color) ? DISPLAY_CTL_COLOR_MODE : 0;
    state->fpga->display->control = (state->control | DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
}

void
playback_cleanup(struct pipeline_state *state)
{
    int command = PLAYBACK_PIPE_EXIT;
    struct timespec ts = {1, 0};
    write(playback_pipe, &command, sizeof(command));
    pthread_timedjoin_np(state->playthread, NULL, &ts);
}
