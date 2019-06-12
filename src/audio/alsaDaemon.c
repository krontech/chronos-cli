/****************************************************************************
 *  Copyright (C) 2018 Kron Technologies Inc <http://www.krontech.ca>.      *
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
 * 		and the built in microphone and speaker on the Chronos 1.4 Camera.
 * 		It can be used to verify that all of the audio hardware is working correctly,
 * 		and outputs <timestamp>.wav files to /tmp/audio whenever video is being recorded.
 *
 * Compile:	arm-linux-gnueabi-gcc (v 5.4.0) for Debian builds with kernel 3.2.0
 *
 * Notes:	- The camera must have the alsa-utils package installed for this code to work.
 * 		- To play audio that has been recorded, use aplay <audio file>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define DAEMON

int main (void)
{

	FILE *lineOutGPIO_fp, *lineInGPIO_fp, *rearRecordingLightGPIO_fp;
	int lineOutVal, lineInVal, recordingVal; //lineInVal and lineOutVal are active low
	int lineInValLast = -1, lineOutValLast = -1;
	int isRecording = 0;
	char timeStampStr[100];
	char recordCmdStr[100];

#ifdef DAEMON
	pid_t pid, sid;

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	/* If we got a good PID, then
	 we can exit the parent process. */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		/* Log any failures here */
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory */
	if ((chdir("/")) < 0) {
		/* Log any failures here */
		exit(EXIT_FAILURE);
	}

	/* Close out the standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
#endif

	//Initialize MicBias GPIO - output
	system("echo 15 > /sys/class/gpio/export");
	system("echo 'out' > /sys/class/gpio/gpio15/direction");
	system("echo 1 > /sys/class/gpio/gpio15/value");

	//Initialize Line In GPIO - input for jack detection
	system("echo 19 > /sys/class/gpio/export");
	system("echo 'in' > /sys/class/gpio/gpio19/direction");

	//Initialize Line Out GPIO - input for jack detection
	system("echo 9 > /sys/class/gpio/export");
	system("echo 'in' > /sys/class/gpio/gpio9/direction");

	//Create folder for recorded wav files to be saved in
	system("mkdir /tmp/audio");

	while(1){

		//Open GPIO value file descriptors:
		lineOutGPIO_fp = fopen("/sys/class/gpio/gpio9/value", "r");
		lineInGPIO_fp = fopen("/sys/class/gpio/gpio19/value", "r");
		rearRecordingLightGPIO_fp = fopen("/sys/class/gpio/gpio25/value","r");

		//get resulting values
		lineOutVal = fgetc(lineOutGPIO_fp) - 48;
		lineInVal = fgetc(lineInGPIO_fp) - 48;
		recordingVal = fgetc(rearRecordingLightGPIO_fp) - 48; //Use the rear red LED as a audio trigger for testing purposes

		//Automatically switch between line in and mic: 1 = line in unplugged, 0 = line in plugged
		if(lineInValLast != lineInVal){
			lineInValLast = lineInVal;
			if(lineInVal){
				system("amixer sset 'Right PGA Mixer Line1L' off");
				system("amixer sset 'Right PGA Mixer Line1R' off");
				system("amixer sset 'Right PGA Mixer Mic3L' on");
				system("amixer sset 'Right PGA Mixer Mic3R' on");
			} else {
				system("amixer sset 'Right PGA Mixer Line1L' on");
				system("amixer sset 'Right PGA Mixer Line1R' on");
				system("amixer sset 'Right PGA Mixer Mic3L' off");
				system("amixer sset 'Right PGA Mixer Mic3R' off");
			}
		}

		//Automatically switch between line out and speaker: 1 = line out unplugged, 0 = line out plugged
		if(lineOutValLast != lineOutVal){
			lineOutValLast = lineOutVal;
			if(lineOutVal){
				system("amixer sset 'Right HPCOM Mux' 'differential of HPLCOM'");
			} else {
				system("amixer sset 'Right HPCOM Mux' 'differential of HPROUT'");
			}
		}

		if(recordingVal && isRecording == 0){
			//build timestamp for filename
			time_t now = time(NULL);
			struct tm *t = localtime(&now);
			strftime(timeStampStr, sizeof(timeStampStr)-1,"%d-%m-%Y_%H:%M:%S",t);

			//turn off MicBias to reduce noise from line in
			if(lineInVal){
				system("echo 0 > /sys/class/gpio/gpio15/value");
			}

			sprintf(recordCmdStr,"arecord -f cd /tmp/audio/%s.wav &", timeStampStr);
			system(recordCmdStr);
			isRecording = 1;
		}

		if(recordingVal == 0 && isRecording){
			system("killall arecord");

			//turn on MicBias to capture line in
			if(lineInVal){
				system("echo 1 > /sys/class/gpio/gpio15/value");
			}

			isRecording = 0;
		}

		//Close GPIO value file descriptors:
		fclose(lineOutGPIO_fp);
		fclose(lineInGPIO_fp);
		fclose(rearRecordingLightGPIO_fp);

		usleep(16667); // 1 frame at 60fps, should capture audio sync'ed to +/- 1 frame.
	}

	exit(EXIT_SUCCESS);
}
