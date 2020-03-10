/****************************************************************************
 *  Copyright (C) 2020 Kron Technologies Inc <http://www.krontech.ca>.      *
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

/* File: 	alsaDaemon.c
 * Author: 	smaharaj@krontech.ca
 * Date: 	June 12th, 2019.
 *
 * Desc: 	This program handles automatic switching between line in/out
 * 		and the built in microphone and speaker on Chronos cameras.
 *
 * Compile:	arm-linux-gnueabi-gcc (v 5.4.0) for Debian builds with kernel 3.2.0
 *
 * Notes:	- The camera must have the alsa-utils package installed for this code to work.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "pipeline.h"

static int
audiomux_setup_input(int gpionum)
{
    FILE *fp;
    char path[PATH_MAX];

    /* Export the GPIO, if not already done. */
    fp = fopen("/sys/class/gpio/export", "w");
    if (fp) {
        fprintf(fp, "%d", gpionum);
        fclose(fp);
    }

    /* Configure for input. */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpionum);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "in");
        fclose(fp);
    }

    /* And finally, open the GPIO file descriptor */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpionum);
    return open(path, O_RDONLY);
}

static int
audiomux_setup_output(int gpionum, int val)
{
    FILE *fp;
    char path[PATH_MAX];

    /* Export the GPIO, if not already done. */
    fp = fopen("/sys/class/gpio/export", "w");
    if (fp) {
        fprintf(fp, "%d", gpionum);
        fclose(fp);
    }

    /* Configure for input. */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpionum);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "out");
        fclose(fp);
    }
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpionum);
    fp = fopen(path, "w");
    if (fp) {
        fputc(val ? '1' : '0', fp);
        fclose(fp);
    }

    /* And finally, open the GPIO file descriptor */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpionum);
    return open(path, O_WRONLY | O_NONBLOCK);
}

static void *
audiomux_thread(void *arg)
{
    struct pipeline_state *state = arg;
    /* GPIO File descriptors */ 
    int mic_bias_fd = audiomux_setup_output(15, 1);
    int line_in_fd = audiomux_setup_input(19);
    int line_out_fd = audiomux_setup_input(9);
    /* GPIO Values */
    int line_in_val = -1;
    int line_out_val = -1;

    fprintf(stderr, "DEBUG: Launching audiomux thread\n");

    /* TODO: Make this use the poll system call to reduce CPU usage. */
    while (1) {
        char buf[2];

        /* Get the line-in value */
        lseek(line_in_fd, 0, SEEK_SET);
        read(line_in_fd, buf, sizeof(buf));
        if ((buf[0] - '0') != line_in_val) {
            /* Line-in value has changed. */
            line_in_val = buf[0] - '0';
            if (line_in_val) {
                fprintf(stderr, "DEBUG: Selecting input Mic3\n");
                system("amixer sset 'Right PGA Mixer Line1L' off");
                system("amixer sset 'Right PGA Mixer Line1R' off");
                system("amixer sset 'Right PGA Mixer Mic3L' on");
                system("amixer sset 'Right PGA Mixer Mic3R' on");
            } else {
                fprintf(stderr, "DEBUG: Selecting input Line1\n");
                system("amixer sset 'Right PGA Mixer Line1L' on");
                system("amixer sset 'Right PGA Mixer Line1R' on");
                system("amixer sset 'Right PGA Mixer Mic3L' off");
                system("amixer sset 'Right PGA Mixer Mic3R' off");
            }
        }

        /* Get the line-out value. */
        lseek(line_out_fd, 0, SEEK_SET);
        read(line_out_fd, buf, sizeof(buf));
        if ((buf[0] - '0') != line_out_val) {
            /* Line-Out value has changed. */
            line_out_val = buf[0] - '0';
            if (line_out_val) {
                fprintf(stderr, "DEBUG: Selecting output HPLCOM\n");
                system("amixer sset 'Right HPCOM Mux' 'differential of HPLCOM'");
            } else {
                fprintf(stderr, "DEBUG: Selecting output HPLROUT\n");
                system("amixer sset 'Right HPCOM Mux' 'differential of HPROUT'");
            }
        }

        /* Turn on the mic-bias if we are recording via line-in. */
        if (state->playstate == PLAYBACK_STATE_LIVE) {
            /* Turn on mic biasing if we are not capturing line-in */
            lseek(line_in_fd, 0, SEEK_SET);
            write(mic_bias_fd, "1", 1);
        }
        /* Otherwise, Turn off the mic-bias to reduce input noise */
        else {
            lseek(line_in_fd, 0, SEEK_SET);
            write(mic_bias_fd, "0", 1);
        }

        /* 1 frame, at 60 fps should keep us synched to +/- 1 frame */
        usleep(1000000 / LIVE_MAX_FRAMERATE);
    }

    return NULL;
}

/*===============================================
 * Audio Module Setup
 *===============================================
 */
void
audiomux_init(struct pipeline_state *state)
{
    /* Start the playback thread. */
    pthread_create(&state->audiothread, NULL, audiomux_thread, state);
}

void
audiomux_cleanup(struct pipeline_state *state)
{
    /* TODO: Implement Me! */
}
