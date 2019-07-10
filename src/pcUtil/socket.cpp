#include <pthread.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <signal.h>
#include "types.h"
#include "pcutil.h"
#include "comms.h"
#include "socket.h"

int initSocket(void)
{
    int master_socket;
    int client_socket[30];
    int max_clients = UNIX_SOCKET_MAX_CLIENTS;
    struct sockaddr_un address;

    //set of socket descriptors
    fd_set readfds;

    //initialize all client_socket[] to 0 so not checked
    memset(client_socket, 0, sizeof(client_socket));

    //create a master socket
    if( (master_socket = socket(AF_UNIX , SOCK_STREAM , 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    unlink(SOCK_PATH);

    //type of socket created
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, SOCK_PATH);

    size_t len = strlen(address.sun_path) + sizeof(address.sun_family);

    if (bind(master_socket, (struct sockaddr *)&address, len)<0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    //try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    //accept the incoming connection
    size_t addrlen = sizeof(address);

    while(true)
    {
        //clear the socket set
        FD_ZERO(&readfds);

        //add master socket to set
        FD_SET(master_socket, &readfds);
        int max_sd = master_socket;

        //add child sockets to set
        for (int i = 0 ; i < max_clients ; i++)
        {
            //socket descriptor
            int sd = client_socket[i];

            //if valid socket descriptor then add to read list
            if(sd > 0)
                FD_SET( sd , &readfds);

            //highest file descriptor number, need it for the select function
            if(sd > max_sd)
                max_sd = sd;
        }

        //wait for an activity on one of the sockets , timeout is NULL , so wait indefinitely
        int activity = select( max_sd + 1 , &readfds , NULL , NULL , NULL);

        if ((activity < 0) && (errno != EINTR))
        {
            printf("pcUtil: select error");
        }

        //If something happened on the master socket , then its an incoming connection
        if (FD_ISSET(master_socket, &readfds))
        {
            int new_socket = 0;
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            //add new socket to array of sockets
            for (int i = 0; i < max_clients; i++)
            {
                //if position is empty
                if( client_socket[i] == 0 )
                {
                    client_socket[i] = new_socket;
                    printf("Adding to list of sockets as %d\n" , i);

                    pthread_t thread_id;
                    if( pthread_create( &thread_id , NULL ,  socketConnectionHandler , (void*) &client_socket[i]) < 0)
                    {
                        perror("pcUtil: could not create thread");
                        return -1;
                    }
                    pthread_detach(thread_id);

                    break;
                }
            }
        }

    }
}

void *socketConnectionHandler(void *socket_desc)
{
    //Get the socket descriptor
    int& sock = *(int*)socket_desc;
    int numBytesRead;
	char str[UNIX_SOCKET_BUFFER_SIZE] = {'\0'};
    int retVal;
    int done;
    BatteryData bd;

	done = 0;
	do {

		memset(str,'\0',UNIX_SOCKET_BUFFER_SIZE); //clear the buffer before receiving new data
		numBytesRead = recv(sock, str, UNIX_SOCKET_BUFFER_SIZE, MSG_DONTWAIT);

		if (numBytesRead <= 0)
		{

			retVal = getBatteryData(&bd);

			if(retVal)
			{
				sprintf(str,
						"battCapacityPercent %d\nbattSOHPercent %d\nbattVoltage %d\nbattCurrent %d\nbattHiResCap %d\nbattHiResSOC %d\n"
								"battVoltageCam %d\nbattCurrentCam %d\nmbTemperature %d\nflags %d\nfanPWM %d\n",
						bd.battCapacityPercent, bd.battSOHPercent,
						bd.battVoltage, bd.battCurrent, bd.battHiResCap,
						bd.battHiResSOC, bd.battVoltageCam,
						bd.battCurrentCam, bd.mbTemperature, bd.flags,
						bd.fanPWM);

				//Check for the shutdown flag - can also be done as a command and propagated up to a UI screen
				if(bd.flags & 64)
				{
					system("/sbin/shutdown -h now");
					shutdown();
				}
			}

			if(send(sock, str, UNIX_SOCKET_BUFFER_SIZE, MSG_NOSIGNAL) < 0)
			{
				done = 1;
			}

			memset(str,'\0',UNIX_SOCKET_BUFFER_SIZE); //clear the buffer before receiving new data
			sleep(1); //recv is non-blocking, if no bytes are received, just send the battery data once a second.

		}
		else
		{

			//parse and process received commands here, put requested data into str buffer
			if(0 == strncmp("GET_BATTERY_DATA", str, 16))
			{
				retVal = getBatteryData(&bd);

				if(retVal)
				{
					/* Flag Map
					 * ------------------------------
					 * Bit
					 * 7 - Not Used
					 * 6 - Shutdown Requested by PMIC (NEW)
					 * 5 - Shipping Mode (NEW)
					 * 4 - Overtemp
					 * 3 - Auto PWR On
					 * 2 - Charging
					 * 1 - AC Plugged In
					 * 0 - Has Battery
					 */
					sprintf(str,
							"battCapacityPercent %d\nbattSOHPercent %d\nbattVoltage %d\nbattCurrent %d\nbattHiResCap %d\nbattHiResSOC %d\n"
									"battVoltageCam %d\nbattCurrentCam %d\nmbTemperature %d\nflags %d\nfanPWM %d\n",
							bd.battCapacityPercent, bd.battSOHPercent,
							bd.battVoltage, bd.battCurrent, bd.battHiResCap,
							bd.battHiResSOC, bd.battVoltageCam,
							bd.battCurrentCam, bd.mbTemperature, bd.flags,
							bd.fanPWM);

					//Check for the shutdown flag - can also be done as a command and propagated up to a UI screen
					if(bd.flags & 64)
					{
						system("/sbin/shutdown -h now");
						shutdown();
					}

				}
				else
				{
					sprintf(str,"getBatteryData() failed");
				}
			}
			else if (!strncmp(str, "SET_SHIPPING_MODE_ENABLED", 25))
			{
				setShippingMode(TRUE) ? sprintf(str,"shipping mode enabled") : sprintf(str,"setShippingMode() failed");
			}
			else if (!strncmp(str, "SET_SHIPPING_MODE_DISABLED", 26))
			{
				setShippingMode(FALSE) ? sprintf(str,"shipping mode disabled") : sprintf(str,"setShippingMode() failed");
			}
			else if (!strncmp(str, "SET_POWERUP_MODE_0", 18)) //powerup on ac restore disabled, powerdown on ac remove disabled
			{
				setPowerupMode(0) ? sprintf(str,"pwrmode0") : sprintf(str,"setPowerupMode() failed");
			}
			else if (!strncmp(str, "SET_POWERUP_MODE_1", 18)) //powerup on ac restore enabled, powerdown on ac remove disabled
			{
				setPowerupMode(1) ? sprintf(str,"pwrmode1") : sprintf(str,"setPowerupMode() failed");
			}
			else if (!strncmp(str, "SET_POWERUP_MODE_2", 18)) //powerup on ac restore disabled, powerdown on ac remove enabled
			{
				setPowerupMode(2) ? sprintf(str,"pwrmode2") : sprintf(str,"setPowerupMode() failed");
			}
			else if (!strncmp(str, "SET_POWERUP_MODE_3", 18)) //powerup on ac restore enabled, powerdown on ac remove enabled
			{
				setPowerupMode(3) ? sprintf(str,"pwrmode3") : sprintf(str,"setPowerupMode() failed");
			}
			else if (!strncmp(str, "SET_FAN_AUTO", 12))
			{
				setFanOverrideMode(FALSE, 128) ? sprintf(str,"disabled fan override") : sprintf(str,"setFanOverrideMode() failed");
			}
			else if (!strncmp(str, "SET_FAN_OFF", 11))
			{
				setFanOverrideMode(TRUE, 0) ? sprintf(str,"enabled fan override") : sprintf(str,"setFanOverrideMode() failed");
			}
			else
			{
				//ignore invalid commands
			}


		}

		if (!done && numBytesRead > 0)
			if (send(sock, str, UNIX_SOCKET_BUFFER_SIZE, 0) < 0)
			{
				perror("send");
				done = 1;
			}
	} while (!done);


    if(numBytesRead == 0)
    {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if(numBytesRead == -1)
    {
        perror("recv failed");
    }

    sock = 0;

    return 0;
}
