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

static void *socketConnectionHandler(void *socket_desc)
{
    //Get the socket descriptor
    int sock = (intptr_t)socket_desc;
    int numBytesRead;
    char str[UNIX_SOCKET_BUFFER_SIZE] = {'\0'};
    int retVal;
    int done;
    BatteryData bd;

    done = 0;
    for (;;) {
        /* Wait up to 1 second for socket activity. */
        fd_set readfds;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        select(sock + 1, &readfds, NULL, NULL, &tv);

        /* Check for any received bytes on the socket */
        memset(str,'\0',UNIX_SOCKET_BUFFER_SIZE); //clear the buffer before receiving new data
        numBytesRead = recv(sock, str, UNIX_SOCKET_BUFFER_SIZE, MSG_DONTWAIT);

        /* To receive zero bytes means that the socket has closed. */
        if (numBytesRead == 0) {
            puts("Client disconnected");
            fflush(stdout);
            break;
        }
        /* A negative value indicates an error, or timeout. */
        if (numBytesRead < 0) {
            if (errno != EAGAIN) {
                /* Something bad happened. */
                perror("recv failed");
                break;
            }
            /* Treat a timeout as though we got a request for battery data. */
            strcpy(str, "GET_BATTERY_DATA");
        }
        /* Otherwise, ensure null-termination before checking commands. */
        else {
            if (numBytesRead >= sizeof(str)) numBytesRead = sizeof(str)-1;
            str[numBytesRead] = '\0';
        }

        /* parse and process received commands here, put requested data into str buffer */
        if(0 == strcmp("GET_BATTERY_DATA", str))
        {
            retVal = getBatteryData(&bd);
            if(!retVal) {
                sprintf(str,"getBatteryData() failed");
                continue;
            }
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

            /* Check for the shutdown flag - can also be done as a command and propagated up to a UI screen */
            if(bd.flags & 64) {
                system("/sbin/shutdown -h now");
            }
        }
        else if (!strcmp(str, "SET_SHIPPING_MODE_ENABLED"))
        {
            setShippingMode(TRUE) ? sprintf(str,"shipping mode enabled") : sprintf(str,"setShippingMode() failed");
        }
        else if (!strcmp(str, "SET_SHIPPING_MODE_DISABLED"))
        {
            setShippingMode(FALSE) ? sprintf(str,"shipping mode disabled") : sprintf(str,"setShippingMode() failed");
        }
        else if (!strcmp(str, "SET_POWERUP_MODE_0")) //powerup on ac restore disabled, powerdown on ac remove disabled
        {
            setPowerupMode(0) ? sprintf(str,"pwrmode0") : sprintf(str,"setPowerupMode() failed");
        }
        else if (!strcmp(str, "SET_POWERUP_MODE_1")) //powerup on ac restore enabled, powerdown on ac remove disabled
        {
            setPowerupMode(1) ? sprintf(str,"pwrmode1") : sprintf(str,"setPowerupMode() failed");
        }
        else if (!strcmp(str, "SET_POWERUP_MODE_2")) //powerup on ac restore disabled, powerdown on ac remove enabled
        {
            setPowerupMode(2) ? sprintf(str,"pwrmode2") : sprintf(str,"setPowerupMode() failed");
        }
        else if (!strcmp(str, "SET_POWERUP_MODE_3")) //powerup on ac restore enabled, powerdown on ac remove enabled
        {
            setPowerupMode(3) ? sprintf(str,"pwrmode3") : sprintf(str,"setPowerupMode() failed");
        }
        else if (!strcmp(str, "GET_POWERUP_MODE"))
        {
            uint8 mode;
            getPowerupMode(&mode) ? sprintf(str, "powerMode %d\n", mode) : sprintf(str, "powerMode failed\n");
        }
        else if (!strcmp(str, "GET_FAN_MODE"))
        {
            BOOL enable;
            uint8 speed;
            if (!getFanOverrideMode(&enable, &speed)) {
                sprintf(str, "fanOverride failed\n");
            }
            else if (!enable) {
                sprintf(str, "fanOverride auto\n");
            }
            else {
                sprintf(str, "fanOverride %d\n", speed);
            }
        }
        else if (!strcmp(str, "SET_FAN_AUTO"))
        {
            setFanOverrideMode(FALSE, 128) ? sprintf(str,"disabled fan override") : sprintf(str,"setFanOverrideMode() failed");
        }
        else if (!strcmp(str, "SET_FAN_OFF"))
        {
            setFanOverrideMode(TRUE, 0) ? sprintf(str,"enabled fan override") : sprintf(str,"setFanOverrideMode() failed");
        }
        else if (!memcmp(str, "SET_FAN_", 8))
        {
            // Assume it's of the form SET_FAN_xxx, where xxx = the specific fan speed to set in the range of 0-255
            unsigned long speed = strtoul(str + 8, NULL, 10);
            setFanOverrideMode(TRUE, speed) ? sprintf(str,"enabled fan override") : sprintf(str,"setFanOverrideMode() failed");
        }
        else if (!strcmp(str, "GET_SHUTDOWN_REASON"))
        {
            uint8 reason;
            getLastShutdownReason(&reason) ? sprintf(str, "lastShutdownReason %d\n", reason) : sprintf(str, "getLastShutdownReason() failed\n");
        }
        else
        {
            //ignore invalid commands
            continue;
        }

        /* Send the response back to the client. */
        if (send(sock, str, UNIX_SOCKET_BUFFER_SIZE, 0) < 0) {
            perror("send");
            break;
        }
    }

    /* Cleanup the socket. */
    close(sock);
    sock = 0;
    return 0;
}

int initSocket(void)
{
    int master_socket;
    int client_socket[30];
    struct sockaddr_un address;
    size_t addrlen = sizeof(address);

    /* Create a master socket */
    if ((master_socket = socket(AF_UNIX , SOCK_STREAM , 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    unlink(SOCK_PATH);

    /* Bind the UNIX socket to a well-known filesystem path */
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, SOCK_PATH);
    if (bind(master_socket, (struct sockaddr *)&address, addrlen) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    /* Listen for new connections, with up to 3 pending connections. */
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    /* Accept incomming connections */
    while (TRUE) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        
        /* Wait for a new connection on the master socket. */
        int activity = select(master_socket + 1, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            printf("pcUtil: select error");
        }

        /* Handle incomming connections on the master socket. */
        if (FD_ISSET(master_socket, &readfds)) {
            pthread_t thread_id;
            int new_socket = 0;
            addrlen = sizeof(struct sockaddr_un);
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            if( pthread_create( &thread_id , NULL ,  socketConnectionHandler , (void*)new_socket) < 0) {
                perror("pcUtil: could not create thread");
                close(new_socket);
                continue;
            }
            pthread_detach(thread_id);
        }
    }
}
