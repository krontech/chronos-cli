#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "api/cam-rpc.h"

/**
 * This function will be called repeatedly from within the mainloop
 * timer launch code.
 *
 * The function will start with two statically initialized variables
 * (int and double) which will be incremented after each time this
 * function runs and will use the setvalue* remote methods to set the
 * new values. If the set methods fail, program is not aborted, but an
 * message will be issued to the user describing the error.
 */
static gboolean
timerCallback(DBusGProxy* proxy)
{
    struct cam_sensor_data info;
    unsigned int i;

    memset(&info, 0, sizeof(info));
    if (cam_get_sensor_data(proxy, &info) != 0) {
        g_print("dbus call failed for cam_get_sensor_data\n");
    }

    /* Print out the sensor data. */
    g_print("Got Image Sensor Data for %s\n", info.name);
    g_print("\thMaxRes: %ld\n", info.hmax);
    g_print("\tvMaxRes: %ld\n", info.vmax);
    g_print("\thMinRes: %ld\n", info.hmin);
    g_print("\tvMinRes: %ld\n", info.vmin);
    g_print("\thIncrement: %ld\n", info.hincrement);
    g_print("\tvIncrement: %ld\n", info.vincrement);
    g_print("\tminExposureNsec: %ld\n", info.expmin);
    g_print("\tmaxExposureNsec: %ld\n", info.expmax);
    g_print("\tvalidGain: [");
    for (i = 0; i < info.n_gains; i++) {
        g_print(i ? ", %d" : "%d", info.gains[i]);
    }
    g_print("]\n");
    return TRUE;
}

/**
 * The test program itself.
 *
 * 1) Setup GType/GSignal
 * 2) Create GMainLoop object
 * 3) Connect to the Session D-Bus
 * 4) Create a proxy GObject for the remote Value object
 * 5) Start a timer that will launch timerCallback once per second.
 * 6) Run main-loop (forever)
 */
int main(int argc, char** argv) {
    DBusGProxy* proxy;
    GMainLoop* mainloop;
    GError* error = NULL;

    /* Initialize the GType/GObject system. */
    g_type_init();
    proxy = cam_proxy_new();
    mainloop = g_main_loop_new(NULL, FALSE);
    if (mainloop == NULL) {
        perror("Main loop creation failed");
        return -1;
    }

    proxy = cam_proxy_new();
    if (proxy == NULL) {
        perror("Failed to get a proxy for D-Bus\n");
        return -1;
    }

    g_print("%s: main Starting main loop (first timer in 1s).\n", argv[0]);
    g_timeout_add(1000, (GSourceFunc)timerCallback, proxy);
    g_main_loop_run(mainloop);
    return EXIT_FAILURE;
}
