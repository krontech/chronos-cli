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
#define PLAYBACK_PIPE_DELAY     (INT_MIN + 4)   /* Delay for 100ms. */

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
    const unsigned int vBackPorch = 8;
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

    /* Calculate the ideal hPeriod and then calculate the matching vPeriod for this framerate. */
    hPeriod = hSync + hBackPorch + state->source.hres + hFrontPorch;
    vPeriod = pxClock / (hPeriod * maxfps);
    if (vPeriod < (state->source.vres + vBackPorch + vSync + vFrontPorch)) {
        vPeriod = (state->source.vres + vBackPorch + vSync + vFrontPorch);
    }

    /* Calculate the actual FPS. */
    state->source.rate = pxClock / (vPeriod * hPeriod);
    fprintf(stderr, "Setup display timing: %d*%d@%d (%u*%u max: %u)\n",
           (hPeriod - hBackPorch - hSync - hFrontPorch),
           (vPeriod - vBackPorch - vSync - vFrontPorch),
           state->source.rate, state->source.hres, state->source.vres, maxfps);

    /* Configure the FPGA */
    state->fpga->display->h_res = state->source.hres;
    state->fpga->display->h_out_res = state->source.hres;
    state->fpga->display->v_res = state->source.vres;
    state->fpga->display->v_out_res = state->source.vres;

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
    unsigned long playend = state->playstart + state->playlength;
    long next = state->position + delta;

    /* Do a no-op instead of dividing by zero. */
    if ((state->position < 0) || (state->playlength == 0)) {
        return;
    }

    /* Advance the logical frame number. */
    if (delta > 0) {
        if (next < playend) {
            state->position = next;
        }
        else if (state->playloop) {
            next = (next - state->playstart) % state->playlength;
            state->position = next + state->playstart;
        }
        else {
            /* Return to live display. */
            int command = PLAYBACK_PIPE_LIVE;
            write(playback_pipe, &command, sizeof(command));
            state->position = state->playstart;
        }
    }
    else if (delta < 0) {
        if (next >= (long)state->playstart) {
            state->position = next;
        }
        else if (state->playloop) {
            /* Loop around to the end. */
            next = (state->playstart - next) % state->playlength;
            state->position = playend - next;
        }
        else {
            /* Return to live display. */
            int command = PLAYBACK_PIPE_LIVE;
            write(playback_pipe, &command, sizeof(command));
            state->position = state->playstart;
        }
    }
}

static void
playback_frame_render(struct pipeline_state *state)
{
    struct video_segment *seg;
    unsigned long address;

    /* If no regions have been set, then just display the first address in memory. */
    if (!state->seglist.totalframes) {
        state->fpga->display->frame_address = state->fpga->seq->region_start;
        state->fpga->display->manual_sync = 1;
        return;
    }

    /* Search the recording for the actual frame address. */
    seg = video_segment_lookup(&state->seglist, state->position, &address);
    if (seg) {
        overlay_update(state, seg);
    } else {
        address = state->fpga->seq->region_start;
    }

    /* Play the frame */
    state->fpga->display->frame_address = address;
    state->fpga->display->manual_sync = 1;
}

/* Signal handler for the playback timer. */
static void
playback_signal(int signo, siginfo_t *info, void *ucontext)
{
    struct pipeline_state *state = cam_pipeline_state();

    /* no-op unless we're in playback and the seek pipe exists. */
    if ((playback_pipe < 0) || (state->playstate != PLAYBACK_STATE_PLAY)) {
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
    /* Paused and live: do nothing. */
    if ((state->playstate == PLAYBACK_STATE_PAUSE) || (state->playstate == PLAYBACK_STATE_LIVE)) {
        return;
    }
    /* Playback mode: Render the next frame. */
    else if (state->playstate == PLAYBACK_STATE_PLAY) {
        /* Do a quicky delta-sigma to set the framerate. */
        int inputrate = state->source.rate ? state->source.rate : LIVE_MAX_FRAMERATE;
        int nextcount = state->playcounter + state->playrate;
        state->playcounter = nextcount % inputrate;
        playback_frame_seek(state, nextcount / inputrate);
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
            g_object_get(G_OBJECT(state->vidsrc), "buffer-level", &state->buflevel, NULL);
        }
    }
    /* If the buffer level was high - play the next frame immediately. */
    /* This tends to improve throughput by pipelining the OMX round trips. */
    else if (state->buflevel > 2) {
        playback_rate_update(state);
        playback_frame_seek(state, 1);
        playback_frame_render(state);
        if (state->vidsrc) {
            g_object_get(G_OBJECT(state->vidsrc), "buffer-level", &state->buflevel, NULL);
        }
    }
    /* Otherwise, wait for more buffers to become available and then play the next frame. */
    else {
        /* Wait for flow control issues to clear. */
        while (state->vidsrc) {
            state->buflevel = 10;
            g_object_get(G_OBJECT(state->vidsrc), "buffer-level", &state->buflevel, NULL);
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
        return -1;
    }

    /* Check for the new timing engine, use it for the exposure time and frame period. */
    if (state->fpga->timing->version >= 1) {
        seg->metadata.interval = state->fpga->timing->period_time;
        seg->metadata.exposure = state->fpga->timing->exp_abn_time;
        /* The timebase depends on the attached sensor. */
        switch (state->board_rev >> 8) {
            case 0x28:  /* LUX2810 */
            case 0x21:  /* LUX2100 */
                seg->metadata.timebase = 75000000; /* 75 MHz */
                break;
            
            case 0x14:  /* LUX1310 New Mainboards */
            case 0x00:  /* LUX1310 Legacy Boards */
            default:    /* Unknown Sensors */
                seg->metadata.timebase = 90000000; /* 90 MHz */
                break;
        }
    }
    /* Otherwise, fall-back to the old timing engine, which may not be accurate. */
    else {
        seg->metadata.interval = state->fpga->sensor->frame_period;
        seg->metadata.exposure = state->fpga->sensor->int_time;
        seg->metadata.timebase = FPGA_TIMEBASE_HZ; /* FPGA internal timing is used */
    }

    return 1;
}

/*===============================================
 * Playback State Changes
 *===============================================
 */
/* Immediately pause playback. */
void
playback_pause(struct pipeline_state *state)
{
    uint32_t control = state->control;

    fprintf(stderr, "Pausing playback\n");
    
    state->playstate = PLAYBACK_STATE_PAUSE;
    overlay_setup(state);
    control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
    control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
    state->fpga->display->control = control;
}

void
playback_seek(struct pipeline_state *state, int delta)
{
    write(playback_pipe, &delta, sizeof(delta));
}

/* Pass a delay command to the playback thread which helps to synchronize the OMX camera. */
void
playback_delay(struct pipeline_state *state)
{
    playback_seek(state, PLAYBACK_PIPE_DELAY);
}

/* This would typically be called from outside the playback thread to change operation. */
void
playback_preroll(struct pipeline_state *state)
{
    fprintf(stderr, "Prerolling playback\n");
    playback_seek(state, PLAYBACK_PIPE_PREROLL);
} /* playback_preroll */

/* Switch to live display mode. */
void
playback_live(struct pipeline_state *state)
{
    playback_seek(state, PLAYBACK_PIPE_LIVE);
}

/* Switch to playback mode, with the desired position and playback rate. */
void
playback_play(struct pipeline_state *state, unsigned long frame, int framerate)
{
    int delta = 0;

    /* Update the frame position and then seek zero frames forward. */
    state->playrate = framerate;
    state->playcounter = 0;
    state->playstart = 0;
    state->playlength = state->seglist.totalframes;
    state->playloop = 1;
    state->position = frame;
    write(playback_pipe, &delta, sizeof(delta));
}

/* Start playback at a given framerate and loop over a subset of frames. */
void
playback_loop(struct pipeline_state *state, unsigned long start, int framerate, unsigned long count)
{
    int delta = 0;
    if (count > state->seglist.totalframes) count = state->seglist.totalframes;

    /* Update the frame position and then seek zero frames forward. */
    state->playrate = framerate;
    state->playcounter = 0;
    state->playstart = start;
    state->playlength = count;
    state->playloop = 1;
    state->position = start;
    write(playback_pipe, &delta, sizeof(delta));
}

void
playback_play_once(struct pipeline_state *state, unsigned long start, int framerate, unsigned long count)
{
    int delta = 0;
    if (count > state->seglist.totalframes) count = state->seglist.totalframes;

    /* Update the frame position and then seek zero frames forward. */
    state->playrate = framerate;
    state->playcounter = 0;
    state->playstart = start;
    state->playlength = count;
    state->playloop = 0;
    state->position = start;
    write(playback_pipe, &delta, sizeof(delta));
}

/* Pass a command to the playback thread to discard all recorded segments. */
void
playback_flush(struct pipeline_state *state)
{
    playback_seek(state, PLAYBACK_PIPE_FLUSH);
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
static void
playback_signal_segment(struct pipeline_state *state)
{
    dbus_signal_segment(state->video);
    if (FPGA_VERSION_REQUIRE(state->fpga->config, 3, 22)) {
        /* Generate parameter updates on FPGA version 3.22 and newer. */
        const char *names[] = {
            "totalFrames",
            "totalSegments",
            "videoSegments",
            NULL
        };
        dbus_signal_update(state->video, names);
    }
}

static void
playback_signal_state(struct pipeline_state *state)
{
    const char *names[] = {
        "videoState",
        NULL
    };
    dbus_signal_update(state->video, names);
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
        pthread_mutex_lock(&state->segmutex);
        while (!(state->fpga->seq->status & SEQ_STATUS_FIFO_EMPTY)) {
            if (playback_region_add(state) > 0) newsegs++;
        }
        pthread_mutex_unlock(&state->segmutex);
        /* Emit a signal from the main loop if there were new segments.  */
        if (newsegs) {
            playback_signal_segment(state);
        }

        /*===============================================
         * Keep an Eye on the Live Video Geometry.
         *===============================================
         */
        if (state->playstate == PLAYBACK_STATE_LIVE) {
            if ((state->fpga->imager->hres_count != state->source.hres) ||
                (state->fpga->imager->vres_count != state->source.vres)) {
                /* The video system probably requires a restart. */
                state->source.hres = state->fpga->imager->hres_count;
                state->source.vres = state->fpga->imager->vres_count;
                pthread_kill(state->mainthread, SIGHUP);
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
            else if (delta == PLAYBACK_PIPE_DELAY) {
                /* Delay prerolling/playback to help sync filesaves. */
                usleep(100000);
            }
            else if (delta == PLAYBACK_PIPE_PREROLL) {
                /* Setup prerolling for filesaves. */
                state->playstate = PLAYBACK_STATE_FILESAVE;
                state->preroll = 3;
                state->fpga->display->pipeline |= DISPLAY_PIPELINE_TEST_PATTERN;
                control |= (DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                control &= ~(DISPLAY_CTL_FOCUS_PEAK_ENABLE | DISPLAY_CTL_ZEBRA_ENABLE);
                state->fpga->display->control = control;

                playback_setup_timing(state, SAVE_MAX_FRAMERATE);
                playback_signal_state(state);
                overlay_setup(state);

                /* Start playback by faking a fsync edge. */
                pfd[0].revents |= POLLPRI;
            }
            else if (delta == PLAYBACK_PIPE_FLUSH) {
                /* Drop all recording segments. */
                video_segment_flush(&state->seglist);
            }
            else if (delta == PLAYBACK_PIPE_LIVE) {
                /* Update the display timing if not already live. */
                if (state->playstate != PLAYBACK_STATE_LIVE) {
                    if ((state->source.hres * state->source.vres) >= 1750000) {
                        /* At about 1.75MP and higher, we must reduce the speed of playback
                         * due to saturation of DDR memory with image data from the sensor.
                         */
                        playback_setup_timing(state, LIVE_MAX_FRAMERATE/2);
                    } else {
                        playback_setup_timing(state, LIVE_MAX_FRAMERATE);
                    }
                }

                /* Clear address select and sync inhibit to enter live display mode. */
                overlay_clear(state);

                control &= ~(DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);
                state->fpga->display->control = control;
                state->playstate = PLAYBACK_STATE_LIVE;

                playback_signal_state(state);
            }
            else {
                /* Update the display timing if not already in playback. */
                if (state->playstate != PLAYBACK_STATE_PLAY) {
                    if ((state->source.hres * state->source.vres) >= 1750000) {
                        /* At about 1.75MP and higher, we must reduce the speed of playback
                         * due to saturation of DDR memory with image data from the sensor.
                         */
                        playback_setup_timing(state, LIVE_MAX_FRAMERATE/2);
                    } else {
                        playback_setup_timing(state, LIVE_MAX_FRAMERATE);
                    }

                    /* Start playback by faking a fsync edge. */
                    pfd[0].revents |= POLLPRI;

                    /* Signal a state change. */
                    state->playstate = PLAYBACK_STATE_PLAY;
                    playback_signal_state(state);
                }

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
            if (state->playstate != PLAYBACK_STATE_PAUSE) {
                fprintf(stderr, "Warning: Playback watchdog expired - retrying frame!\n");
                state->fpga->display->manual_sync;
            }
            watchdog = PLAYBACK_WATCHDOG_COUNT;
        }
    }

    /* Cleanup */
    video_segment_flush(&state->seglist);
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
    video_segments_init(&state->seglist, 0, 0, state->fpga->seq->frame_size);
    pthread_mutex_init(&state->segmutex, NULL);

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

    /* Start off paused. */
    state->playstate = PLAYBACK_STATE_PAUSE;
    state->control = (state->source.color) ? DISPLAY_CTL_COLOR_MODE : 0;
    state->fpga->display->control = (state->control | DISPLAY_CTL_ADDRESS_SELECT | DISPLAY_CTL_SYNC_INHIBIT);

    /* Start the playback thread. */
    pthread_create(&state->playthread, NULL, playback_thread, state);
}

void
playback_cleanup(struct pipeline_state *state)
{
    int command = PLAYBACK_PIPE_EXIT;
    struct timespec ts = {1, 0};
    write(playback_pipe, &command, sizeof(command));
    pthread_timedjoin_np(state->playthread, NULL, &ts);
    pthread_mutex_destroy(&state->segmutex);
}
