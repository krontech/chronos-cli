/****************************************************************************
 *  Copyright (C) 2017-2018 Kron Technologies Inc <http://www.krontech.ca>. *
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
#ifndef _CAMERA_H
#define _CAMERA_H

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#define CAMERA_SERIAL_I2CADDR   0x54
#define CAMERA_SERIAL_LENGTH    32
#define CAMERA_SERIAL_OFFSET    0

/* Some forward declarations. */
struct fpga;
struct ioport;
struct image_sensor;
struct image_geometry;

typedef struct {
    GObject parent;

    /* Camera internals. */
    struct fpga *fpga;
    const struct ioport *iops;
    struct image_sensor *sensor;
    unsigned long long mem_gbytes;
    char serial[CAMERA_SERIAL_LENGTH];

    /* Color and Whitebalance */
    double softgain;
    double cc_matrix[9];
    double wb_matrix[3];
} CamObject;

typedef struct {
    GObjectClass parent;
} CamObjectClass;

GType cam_object_get_type(void);

#define CAM_OBJECT_TYPE    (cam_object_get_type())

/* Init Functions */
unsigned int mem_init(struct fpga *fpga);
int cam_init(CamObject *cam);
int trig_init(CamObject *cam);
int dbus_init(CamObject *cam);

/* Runtime Stuff */
void cal_update_color_matrix(CamObject *cam, double softgain);
int cal_load_fpn(CamObject *cam, struct image_geometry *g);
int cal_load_dcg(CamObject *cam, int sensor_gain);
int cal_load_gain(CamObject *cam, int sensor_gain);

/* Default color matricies */
extern const double ccm_default_color[9];
extern const double ccm_default_mono[9];
extern const double wb_default_color[3];
extern const double wb_default_mono[3];

#endif /* _CAMERA_H */
