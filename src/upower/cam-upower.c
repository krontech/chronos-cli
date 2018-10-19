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

#define DEFAULT_BMS_FIFO_PATH   "/var/run/bmsFifo"

sig_atomic_t caught_sighup = 0;
sig_atomic_t caught_sigint = 0;

static void
catch_signal(int signo, siginfo_t *info, void *ucontext)
{
    if (signo == SIGINT) {
        caught_sigint = 1;
    }
    if (signo == SIGHUP) {
        caught_sighup = 1;
    }
    if ((signo == SIGUSR1) && (info->si_code == SI_QUEUE)) {
        pwrcmd_command(info->si_int, PWRCMD_GET_DATA_EXT);
    }
    if ((signo == SIGALRM) && (info->si_code == SI_TIMER)) {
        pwrcmd_command(info->si_int, PWRCMD_GET_DATA_EXT);
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

/* Write the battery data to the FIFO, or return -1 and set errno as to why. */
static int
bms_write_fifo(int fd, const BatteryData *data)
{
    if (dprintf(fd, "battCapacityPercent %d\n", data->battCapacityPercent) < 0) return -1;
    if (dprintf(fd, "battSOHPercent %d\n",      data->battSOHPercent) < 0) return -1;
    if (dprintf(fd, "battVoltage %d\n",         data->battVoltage) < 0) return -1;
    if (dprintf(fd, "battCurrent %d\n",         data->battCurrent) < 0) return -1;
    if (dprintf(fd, "battHiResCap %d\n",        data->battHiResCap) < 0) return -1;
    if (dprintf(fd, "battHiResSOC %d\n",        data->battHiResSOC) < 0) return -1;
    if (dprintf(fd, "battVoltageCam %d\n",      data->battVoltageCam) < 0) return -1;
    if (dprintf(fd, "battCurrentCam %d\n",      data->battCurrentCam) < 0) return -1;
    if (dprintf(fd, "mbTemperature %d\n",       data->mbTemperature) < 0) return -1;
    if (dprintf(fd, "flags %d\n",               data->flags) < 0) return -1;
    if (dprintf(fd, "fanPWM %d\n",              data->fanPWM) < 0) return -1;
    return 0;
}

static void
usage(FILE *fp, int argc, char *argv[])
{
    fprintf(fp, "Usage: %s [options] [DEVICE]\n\n", argv[0]);

    fprintf(fp, "Connect to the power controller via a serial port at DEVICE,\n");
    fprintf(fp, "and report the battery and charge status.\n\n");

    fprintf(fp, "options:\n");
    fprintf(fp, "\t-p, --poll IVAL  poll for battery data every IVAL milliseconds (defualt: 500)\n");
    fprintf(fp, "\t-f, --fifo PATH  write battery data to a FIFO at PATH (default: %s)\n", DEFAULT_BMS_FIFO_PATH);
    fprintf(fp, "\t-d, --daemon     fork and detach process to run in the backround\n");
    fprintf(fp, "\t-h, --help       display this help and exit\n");
} /* usage */

int
main(int argc, char * argv[])
{
    int fd, fifofd = -1;
    struct sigaction sigact;
    struct termios tty;
    size_t offset;
    timer_t polltimer;
    uint8_t rxbuf[1024];

    /* Default arguments. */
    const char *devpath = "/dev/ttyO0";
    const char *bmsFifo = "/var/run/bmsFifo";
    unsigned int verbose = 0;
    unsigned int daemon = 0;
    unsigned int pollmsec = 1000;

    /* Option Parsing */
    const char *short_options = "p:f:dvh";
    const struct option long_options[] = {
        {"poll",    required_argument,  0, 'p'},
        {"fifo",    required_argument,  0, 'f'},
        {"daemon",  no_argument,        0, 'd'},
        {"verbose", no_argument,        0, 'v'},
        {"help",    no_argument,        0, 'h'},
        {0, 0, 0, 0}
    };
    char *e;
    int c;

    optind = 0;
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) > 0) {
        switch (c) {
            case 'p':
                pollmsec = strtoul(optarg, &e, 0);
                if (*e != '\0') {
                    fprintf(stderr, "Invalid value for MSEC: \'%s\'\n", optarg);
                    usage(stderr, argc, argv);
                    return EXIT_FAILURE;
                }
                break;

            case 'f':
                bmsFifo = optarg;
                break;

            case 'd':
                daemon = 1;
                break;
            
            case 'v':
                verbose = 1;
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
    fd = open(devpath, O_RDWR | O_NOCTTY | O_SYNC);
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

    /* Catch signals. */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = catch_signal;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGUSR1, &sigact, NULL);
    sigaction(SIGALRM, &sigact, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Create the timer used for refreshing the battery data. */
    if (pollmsec) {
        struct sigevent sigev;
        struct itimerspec its;
        memset(&sigev, 0, sizeof(sigev));

        sigev.sigev_notify = SIGEV_SIGNAL;
        sigev.sigev_signo = SIGALRM;
        sigev.sigev_value.sival_int = fd;
        timer_create(CLOCK_MONOTONIC, &sigev, &polltimer);
        
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 10000000;
        its.it_interval.tv_sec = (pollmsec / 1000);
        its.it_interval.tv_nsec = (pollmsec % 1000) * 1000000;
        timer_settime(polltimer, 0, &its, NULL);
    }

    /* Handle commands from the power controller. */
    offset = 0;
    while (1) {
        int nfds = fd + 1;
        int ret;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        /* Wait for bytes from the power controller. */
        ret = select(nfds, &rfds, NULL, NULL, NULL);
        if (ret == 0) continue;
        if (ret < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "Call to select() failed (%s)\n", strerror(errno));
                break;
            }
            if (caught_sigint) break;
        }

        /* Check for received commands. */
        ret = pwrcmd_try_receive(fd, rxbuf, sizeof(rxbuf), &offset);
        if (ret == 0) continue;
        if (ret < 0) {
            fprintf(stderr, "Failed to read from device (%s)\n", strerror(errno));
            break;
        }

        /* Handle messages from the power controller. */
        switch (rxbuf[0]) {
            case PWRCMD_REQ_POWERDOWN:
                fprintf(stderr, "Shutdown requested\n");
	            system("/sbin/shutdown -h now");
                break;

            case PWRCMD_GET_DATA_EXT:{
                BatteryData data;
                if (pwrcmd_parse_battery(&data, rxbuf, ret) != 0) {
                    break;
                }

                /* Verbose logging of battery data. */
                if (verbose) {
                    fprintf(stderr, "Battery Data Received:\n");
                    fprintf(stderr, "\tbattCapacityPercent %d%%\n", data.battCapacityPercent);
                    fprintf(stderr, "\tbattSOHPercent %d\n",      data.battSOHPercent);
                    fprintf(stderr, "\tbattVoltage %.3f V\n",     data.battVoltage / 1000.0);
                    fprintf(stderr, "\tbattCurrent %d\n",         data.battCurrent);
                    fprintf(stderr, "\tbattHiResCap %d\n",        data.battHiResCap);
                    fprintf(stderr, "\tbattHiResSOC %d\n",        data.battHiResSOC);
                    fprintf(stderr, "\tbattVoltageCam %.3f V\n",  data.battVoltageCam / 1000.0);
                    fprintf(stderr, "\tbattCurrentCam %d\n",      data.battCurrentCam);
                    fprintf(stderr, "\tmbTemperature %.1f C\n",   data.mbTemperature / 10.0);
                    fprintf(stderr, "\tflags");
                    if (data.flags & PWRCMD_FLAG_BATT_PRESENT) fprintf(stderr, " batt");
                    if (data.flags & PWRCMD_FLAG_LINE_POWER) fprintf(stderr, " ac");
                    if (data.flags & PWRCMD_FLAG_CHARGING) fprintf(stderr, " charge");
                    fprintf(stderr, "\n\tfanPWM %d%%\n\n",        (data.fanPWM * 100) / 255);
                }

                /* Output battery data to the FIFO. */
                if (fifofd < 0) {
                    fifofd = open(bmsFifo, O_WRONLY | O_NONBLOCK);
                    if (fifofd < 0) break;
                }
                if (bms_write_fifo(fifofd, &data) < 0) {
                    /* Cleanup the pipe on errors. */
                    if (errno == EAGAIN) break;
                    if ((errno != EPIPE) && verbose) {
                        fprintf(stderr, "Error occured when writing to FIFO (%s)\n", strerror(errno));
                    }
                    close(fifofd);
                    fifofd = -1;
                }
                break;
            }

            default:
                fprintf(stderr, "Unknown power controller command received (0x%02x)\n", rxbuf[0]);
                break;
        }
    }
} /* main */
