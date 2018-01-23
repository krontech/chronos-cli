#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dbus/dbus-glib.h>

#include "api/cam-rpc.h"
#include "camera.h"

#include "i2c.h"
#include "fpga.h"
#include "fpga-sensor.h"

#define EEPROM_I2C_ADDR_CAMERA      0x54

#define EEPROM_CAMERA_SERIAL_OFFSET 0
#define EEPROM_CAMERA_SERIAL_LEN    32

static int
read_serial(char *out, const struct ioport *iops)
{
    int err;
    int fd = open(ioport_find_by_name(iops, "eeprom-i2c"), O_RDWR);
    if (fd < 0) {
        return -1;
    }
    err = i2c_eeprom_read16(fd, CAMERA_SERIAL_I2CADDR, CAMERA_SERIAL_OFFSET, out, CAMERA_SERIAL_LENGTH);
    close(fd);
    return err;
}

int
main(void)
{
    CamObject *cam;
    GMainLoop* mainloop;
    int err;

    /* Init glib */
    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    if (!mainloop) {
        perror("Main loop creation failed");
    }
    cam = g_object_new(CAM_OBJECT_TYPE, NULL);
    cam->iops = board_chronos14_ioports;
    err = read_serial(cam->serial, board_chronos14_ioports);
    if (err < 0) {
        memset(cam->serial, 0, sizeof(cam->serial));
        fprintf(stderr, "Failed to read camera serial number.\n");
    }
    else {
        fprintf(stderr, "Serial Number: %s\n", cam->serial);
    }

    /* Init hardware */
    cam->fpga = fpga_open();
    if (!cam->fpga) {
        perror("FPGA initialization failed");
    }
    cam->fpga->reg[SYSTEM_RESET] = 1;

    cam->mem_gbytes = mem_init(cam->fpga);
    if (cam->mem_gbytes == 0) {
        fpga_close(cam->fpga);
        perror("DDR memory initialization failed");
    }

    cam->sensor = lux1310_init(cam->fpga, board_chronos14_ioports);
    if (!cam->sensor) {
        fpga_close(cam->fpga);
        perror("Image sensor initialization failed");
    }

    //trig_init(cam->sensor, board_chronos14_ioports);
    cam_init(cam->sensor);

    /* Attach the DBus interface and run the mainloop. */
    dbus_init(cam);
    g_main_loop_run(mainloop);
}
