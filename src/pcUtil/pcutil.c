#include <pthread.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <signal.h>
#include "types.h"
#include "pcutil.h"
#include "comms.h"
#include "IntelHex.h"
#include "socket.h"

#include <sys/socket.h>
#include <sys/un.h>


#define error_message printf

void* rxThread(void *arg);
pthread_t rxThreadID;
volatile uint8 terminateRxThread;
int sfd;
int fd;
BatteryData bd;
uint8 shutdownFlag = 0;


int
set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                error_message ("error %d from tcgetattr", errno);
                return -1;
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        //tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY | INPCK | IGNPAR);
        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
                error_message ("error %d from tcsetattr", errno);
                return -1;
        }
        return 0;
}

void
set_blocking (int fd, int should_block)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                error_message ("error %d from tggetattr", errno);
                return;
        }

        tty.c_cc[VMIN]  = should_block ? 1 : 0;
        tty.c_cc[VTIME] = 1;            // 0.1 seconds read timeout

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
                error_message ("error %d setting term attributes", errno);
}

int main(int argc, const char * argv[])
{
	int exitCode = EXIT_SUCCESS;
	int retVal;
	BOOL inBootloader;
	const char *portname = "/dev/ttyO0";
	char pidStr[16];
	int pidLen, res;

	signal(SIGPIPE, SIG_IGN);

    if ( argc < 2 ) // argc should be 2 or greater for correct execution
    {

    	printf("Starting pcUtil in daemon mode. Use --help to see a list of options.\r\n");
    	argv[1] = "-d"; //start pcUtil in daemon mode by default if no args are provided

    	/* Our process ID and Session ID */
    	pid_t pid, sid;

		/* Fork off the parent process */
		pid = fork();
		if (pid < 0)
		{
			printf("pcUtil: Failed to fork process\n");
			exit(EXIT_FAILURE);
		}
		/* If we got a good PID, then
		 we can exit the parent process. */
		if (pid > 0)
		{
			exit(EXIT_SUCCESS);
		}

		/* Change the file mode mask */
		umask(0);

		/* Open any logs here */

		pid = getpid();

		//Write the PID to the PID file
		//Mode bits are required for newer compilers
		fd = open("/var/run/pcUtil.pid", O_WRONLY | O_CREAT, S_IWUSR | S_IRGRP | S_ISGID);

		if(fd == -1)
		{
			printf("pcUtil: Failed to open PID file for writing\n");
			exit(EXIT_FAILURE);
		}

		sprintf(pidStr, "%d\n", pid);

		pidLen = strlen(pidStr);
		res = write(fd, pidStr, pidLen);
		if(res != pidLen)
		{
			printf("pcUtil: Failed to write PID file, returned %d, should have been %d\n", res, pidLen);
			exit(EXIT_FAILURE);
		}

		close(fd);

		/* Create a new SID for the child process */
		sid = setsid();
		if (sid < 0)
		{
			/* Log the failure */
			printf("pcUtil: Failed create SID for child process\n");
			exit(EXIT_FAILURE);
		}

		/* Change the current working directory */
		if ((chdir("/")) < 0)
		{
			/* Log the failure */
			printf("pcUtil: Failed change the working directory\n");
			exit(EXIT_FAILURE);
		}

		/* Close out the standard file descriptors */
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

    }


	//Open serial port
	sfd = open (portname, O_RDWR | O_NOCTTY | O_SYNC);
	if (sfd < 0)
	{
			error_message ("error %d opening %s: %s", errno, portname, strerror (errno));
			return 0;
	}

	set_interface_attribs (sfd, B57600, 0);  // set speed to 57600 bps, 8n1 (no parity)
	set_blocking (sfd, 0);                // set no blocking

	// Start data receive thread
	terminateRxThread = 0;

	retVal = pthread_create(&rxThreadID, NULL, &rxThread, (void *)sfd);
	if(retVal)
	{
		printf("Error creating rxThread. Exiting.\n");
		return 0;
	}

	if(0 == strcmp("-h", argv[1]) || 0 == strcmp("--help", argv[1]))
	{
        // We print argv[0] assuming it is the program name
        printf( "usage: %s [OPTIONS] \r\n", argv[0] );
        printf( "Options:\r\n"
        		"-u <path>  Update firmware using <path> pointing to Intel Hex file\r\n"
        		"-b         Jump to Bootloader\r\n"
        		"-a         Jump to User Application\r\n"
        		"-d         Start Power Management Daemon, output to pcUtil_socket unix socket\r\n"
        		"-i         Check if the power controller is in bootloader\r\n"
        		"-q         Query battery information\r\n"
        		"-pm <mode> Set powerup mode: <mode>: 0: default, 1: powerup on AC restore,\r\n"
        		"            2:powerdown on AC remove, 3: both\r\n"
        		"-qpm       Query current setting of powerup mode\r\n"
        		"-fan <val> Set fan speed override. <val>: \"off\" to disable override,\r\n"
        		"            otherwise 0-255 for off to full speed\r\n"
        		"-qfan      Query current fan speed override setting\r\n"
        		"-sm <mode> Set shipping mode: <mode>: \"on\" to ignore pwr btn until AC is applied,\r\n"
        		"            \"off\" to turn on when power button is pressed\r\n"
        		"-qsm       Query current setting of shipping mode\r\n"
        		"-v         Query current PMIC firmware version\r\n"
        		"-qsr       Query the last shutdown reason\r\n");

    	exit(EXIT_FAILURE);
    	return 1;
	}
	else if(0 == strcmp("-u", argv[1]))
	{
		if(argc < 3)
		{
			printf( "Error: No file specified. Exiting.\r\n");
			exit(EXIT_FAILURE);
			return 1;
		}

		retVal = updateFirmware(argv[2]);

		if(retVal)
		{
			printf("Firmware update complete!\r\n");
		}
		else
		{
			exitCode = EXIT_FAILURE;
			printf("Firmware update failed!\r\n");
		}
	}
	else if(0 == strcmp("-b", argv[1]))
	{
		jumpToBootloader();
	}
	else if(0 == strcmp("-a", argv[1]))
	{
		jumpToProgram();
	}
	else if(0 == strcmp("-i", argv[1]))
	{
		retVal = isInBootloader(&inBootloader);
		if(retVal)
		{
			if(inBootloader) {
				printf("In bootloader\r\n");
				exitCode = EXIT_SUCCESS;
			} else {
				/* It's not really failure - just let the shell know our findings. */
				printf("In application\r\n");
				exitCode = 1;
			}
		}
		else
		{
			printf("isInBootloader() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-pm", argv[1]))	//Set powerup mode
	{
		uint8 mode = atoi(argv[2]);
		retVal = setPowerupMode(mode);
		if(retVal)
		{
			if(mode)
				printf("Enabled powerup on external power connection\r\n");
			else
				printf("Disabled powerup on external power connection\r\n");
		}
		else
		{
			printf("setPowerupMode() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-qpm", argv[1]))	//Query powerup mode
	{
		uint8 mode;

		retVal = getPowerupMode(&mode);

		if(retVal)
		{
			printf("Powerup on external power connection is %s\r\n", (mode & POWERUP_ON_AC_RESTORE) ? "enabled" : "disabled");
			printf("Powerup on external power disconnection is %s\r\n", (mode & POWERUP_ON_AC_REMOVE) ? "enabled" : "disabled");
		}
		else
		{
			printf("getPowerupMode() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-fan", argv[1]))	//Set fan override
	{
		BOOL mode = (0 == strcmp("off", argv[2])) ? FALSE : TRUE;
		uint8 speed = 128;
		if(mode)
			speed = atoi(argv[2]);

		retVal = setFanOverrideMode(mode, speed);
		if(retVal)
		{
			if(mode)
				printf("Set fan speed override to %d\r\n", speed);
			else
				printf("Disabled fan speed override\r\n");
		}
		else
		{
			printf("setFanOverrideMode() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-qfan", argv[1]))	//Query fan override mode/speed
	{
		BOOL mode;
		uint8 speed;

		retVal = getFanOverrideMode(&mode, &speed);

		if(retVal)
		{
			printf("Fan override is %s, speed = %d\r\n", mode ? "enabled" : "disabled", speed);
		}
		else
		{
			printf("getFanOverrideMode() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-d", argv[1])) //start daemon, create unix stream socket
	{

		initSocket();

	}
	else if(0 == strcmp("-q", argv[1]))	//query info
	{
		retVal = getBatteryData(&bd);
		if(retVal)
		{
			printf("Cap: %d, SOH: %d, V: %d, I: %d, HRCap: %d, HRSOC: %d VCam: %d ICam: %d Temp: %d, flags: %x, PWM: %d.\n",
					bd.battCapacityPercent,
					bd.battSOHPercent,
					bd.battVoltage,
					bd.battCurrent,
					bd.battHiResCap,
					bd.battHiResSOC,
					bd.battVoltageCam,
					bd.battCurrentCam,
					bd.mbTemperature,
					bd.flags,
					bd.fanPWM);
		}
		else
		{
			printf("getBatteryData() failed\r\n");
			exitCode = EXIT_FAILURE;
		}

	}
	else if(0 == strcmp("-sm", argv[1])) //set shipping mode
	{
		BOOL mode = (0 == strcmp("on", argv[2])) ? TRUE : FALSE;
		retVal = setShippingMode(mode);

		if(!retVal)
		{
			printf("setShippingMode() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
		else if(mode)
		{
			printf("Enabled shipping mode\r\n");
		}
		else
		{
			printf("Disabled shipping mode\r\n");
		}
	}
	else if(0 == strcmp("-qsm", argv[1])) //query shipping mode
	{
		BOOL mode;

		retVal = getShippingMode(&mode);

		if(retVal)
		{
			printf("Shipping mode is %s", mode ? "enabled\r\n" : "disabled\r\n");
		}
		else
		{
			printf("getShippingMode() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-v", argv[1])) //query PMIC fw version
	{
		uint16 version;

		retVal = getPMICVersion(&version);

		if(retVal)
		{
			printf("PMIC Firmware Version: %d\r\n", version);
		}
		else
		{
			printf("getPMICVersion() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}
	else if(0 == strcmp("-qsr", argv[1])) //query the last shutdown reason
	{
		uint8 reason;

		retVal = getLastShutdownReason(&reason);

		if(retVal)
		{
			printf("Last Shutdown Reason: %d\r\n", reason);
			if(reason == 0) 		printf("Unintentional shutdown\n");
			if(reason & 0b00000010) printf("Low Battery\n");
			if(reason & 0b00000100) printf("Watchdog\n");
			if(reason & 0b00001000) printf("Overtemperature\n");
			if(reason & 0b00010000) printf("Auto Power Off Mode\n");
			if(reason & 0b00100000) printf("Requested by software\n");
			if(reason & 0b01000000) printf("Shutdown via power button\n");
			if(reason & 0b10000000) printf("Force shutdown via power button\n");

			if(reason & 0b00000001)
			{
				printf("PM acknowledged, reached off state.\n");
			}
			else
			{
				printf("PM did not reach off state.\n");
			}
		}
		else
		{
			printf("getLastShutdownReason() failed\r\n");
			exitCode = EXIT_FAILURE;
		}
	}

	close(sfd);

	exit(exitCode);
	return 0;

}

