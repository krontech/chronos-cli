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
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>

#include "pwrcmd.h"
#include "mbcrc16.h"

#define CAM_PWRCTRL_BAUDRATE    B57600

sig_atomic_t caught_sighup = 0;
sig_atomic_t caught_sigint = 0;
sig_atomic_t caught_sigusr1 = 0;

static void
catch_signal(int signo)
{
    if (signo == SIGINT) {
        caught_sigint = 1;
    }
    if (signo == SIGHUP) {
        caught_sighup = 1;
    }
    if (signo == SIGUSR1) {
        caught_sigusr1 = 1;
    }
}

static void
daemonize(void)
{
    pid_t pid, sid;
    FILE *pidfile;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "camshutdown: Failed to fork process: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);

    /* Open any logs here */
    pid = getpid();

    /* Write the PID to the PID file */
    pidfile = fopen("/var/run/camshutdown.pid", "w");
    if (!pidfile) {
        fprintf(stderr, "camshutdown: Failed to open PID file for writing\n");
        exit(EXIT_FAILURE);
    }
    if (fprintf(pidfile, "%d\n", pid) <= 0) {
        fprintf(stderr, "camshutdown: Failed to write PID file\n");
        exit(EXIT_FAILURE);
	}
    fclose(pidfile);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		/* Log the failure */
		fprintf(stderr, "camshutdown: Failed create SID for child process\n");
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory */
	if ((chdir("/")) < 0) {
		/* Log the failure */
		fprintf(stderr, "camshutdown: Failed change the working directory\n");
		exit(EXIT_FAILURE);
	}

	/* Close out the standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}

static void
usage(FILE *fp, int argc, char *argv[])
{
    fprintf(fp, "Usage: %s [options] [DEVICE]\n\n", argv[0]);

    fprintf(fp, "Connect to the power controller via a serial port at DEVICE,\n");
    fprintf(fp, "and report the battery and charge status.\n\n");

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-h, --help         display this help and exit\n");
} /* usage */

int
main(int argc, char * argv[])
{
    int fd;
    struct termios tty;
    size_t offset;
    uint8_t rxbuf[1024];

    /* Default arguments. */
    const char *devpath = "/dev/ttyO0";
    const char *bmsFifo = "/var/run/bmsFifo";
    unsigned int daemon = 0;

    /* Option Parsing */
    const char *short_options = "h";
    const struct option long_options[] = {
        {"poll",    required_argument,  0, 'p'},
        {"fifo",    required_argument,  0, 'f'},
        {"daemon",  no_argument,        0, 'd'},
        {"help",    no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };
    char *e;
    int c;

    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 'f':
                bmsFifo = optarg;
                break;

            case 'd':
                daemon = 1;
                break;

            case 'h':
                usage(stdout, argc, argv);
                return EXIT_SUCCESS;
            case '?':
            default:
                return EXIT_FAILURE;
        }
    }
    /* If there is another argument, parse it as the display resolution. */
    if (optind < argc) {
        devpath = argv[optind];
    }

    /* Open and configure the serial port to the power controller. */
    fd = open (devpath, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open \"%s\" (%s)\n", devpath, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Configure baud rate and parity. */
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0) {
        fprintf(stderr, "Failed to read TTY configuration from \"%s\" (%s)\n", devpath, strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }
    cfsetospeed(&tty, CAM_PWRCTRL_BAUDRATE);
    cfsetispeed(&tty, CAM_PWRCTRL_BAUDRATE);
    cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr (fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Failed to set TTY configuration for \"%s\" (%s)\n", devpath, strerror(errno));
        return EXIT_FAILURE;
    }

    /* create the FIFO for reporting the battery status. */
    mkfifo(bmsFifo, 0666);

    /* Detach from the foreground and daemonize. */
    if (daemon) {
        daemonize();
    }

    /* TODO: DBus power manager interface. */

    /* Catch SIGUSR1 to refresh the battery level. */
    signal(SIGINT, catch_signal);
    signal(SIGUSR1, catch_signal);

    /* Handle commands from the power controller. */
    offset = 0;
    while (1) {
        int nfds = fd + 1;
        int ret;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        ret = select(nfds, &rfds, NULL, NULL, NULL);
        if (ret == 0) continue;
        if (ret < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "Call to select() failed (%s)\n", strerror(errno));
                break;
            }
            if (caught_sigint) break;
            if (caught_sigusr1) {
                pwrcmd_command(fd, PWRCMD_GET_DATA_EXT);
            }
        }

        /* Check for received commands. */
        ret = pwrcmd_receive(fd, rxbuf, sizeof(rxbuf), &offset);
        if (ret == 0) continue;
        if (ret < 0) {
            fprintf(stderr, "Failed to read from device (%s)\n", strerror(errno));
            break;
        }

        /* Handle messages from the power controller. */
        switch (rxbuf[0]) {
            case PWRCMD_REQ_POWERDOWN:
                fprintf(stderr, "Shutdown requested\n");
	            //system("/sbin/shutdown -h now");
                break;

            case PWRCMD_GET_DATA_EXT:{
                BatteryData data;
                if (pwrcmd_parse_battery(&data, rxbuf, ret) == 0) {
                    /* Log battery data */
                    fprintf(stderr, "Battery Data Received:\n");
                    fprintf(stderr, "\tbattCapacityPercent %d\n", data.battCapacityPercent);
                    fprintf(stderr, "\tbattSOHPercent %d\n",      data.battSOHPercent);
                    fprintf(stderr, "\tbattVoltage %d\n",         data.battVoltage);
                    fprintf(stderr, "\tbattCurrent %d\n",         data.battCurrent);
                    fprintf(stderr, "\tbattHiResCap %d\n",        data.battHiResCap);
                    fprintf(stderr, "\tbattHiResSOC %d\n",        data.battHiResSOC);
                    fprintf(stderr, "\tbattVoltageCam %d\n",      data.battVoltageCam);
                    fprintf(stderr, "\tbattCurrentCam %d\n",      data.battCurrentCam);
                    fprintf(stderr, "\tmbTemperature %d\n",       data.mbTemperature);
                    fprintf(stderr, "\tflags");
                    if (data.flags & PWRCMD_FLAG_BATT_PRESENT) fprintf(stderr, " batt");
                    if (data.flags & PWRCMD_FLAG_LINE_POWER) fprintf(stderr, " ac");
                    if (data.flags & PWRCMD_FLAG_CHARGING) fprintf(stderr, " charge");
                    fprintf(stderr, "\n\tfanPWM %d\n\n",            data.fanPWM);
                }
                break;
            }

            default:
                fprintf(stderr, "Unknown power controller command received (0x%02x)\n", rxbuf[0]);
                break;
        }
    }
} /* main */
