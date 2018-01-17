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
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

typedef struct {
    GObject parent;

    /* Camera internals. */
    struct fpga *fpga;
    struct image_sensor *sensor;
} CamObject;

typedef struct {
    GObjectClass parent;
} CamObjectClass;

GType cam_object_get_type(void);

#define CAM_OBJECT_TYPE    (cam_object_get_type())

#define CAM_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_CAST((_obj_), CAM_OBJECT_TYPE, CamObject))
#define CAM_OBJECT_CLASS(_kclass_) \
    (G_TYPE_CHECK_CLASS_CAST((_kclass_), CAM_OBJECT_TYPE, CamObjectClass))
#define CAM_IS_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_TYPE(_obj_), CAM_OBJECT_TYPE))
#define CAM_IS_CLASS(_kclass_) \
    (G_TYPE_CHECK_CLASS_TYPE(_kclass_), CAM_OBJECT_TYPE))
#define CAM_GET_CLASS(_obj_) \
    (G_TYPE_INSTANCE_GET_CLASS(_obj_), CAM_OBJECT_TYPE, CamObjectClass))

G_DEFINE_TYPE(CamObject, cam_object, G_TYPE_OBJECT)