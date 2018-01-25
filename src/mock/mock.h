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
#ifndef _MOCK_H
#define _MOCK_H

typedef struct {
    GObject parent;

    /* Video Settings */
    unsigned long hres;
    unsigned long vres;
    unsigned long hoff;
    unsigned long voff;
    unsigned long long exposure_nsec;
    unsigned long long period_nsec;
    int gain_db;

} MockObject;

typedef struct {
    GObjectClass parent;
} MockObjectClass;

GType mock_object_get_type(void);

#define MOCK_OBJECT_TYPE    (mock_object_get_type())

/* Magical constants pretending to be hardware. */
#define MOCK_MAX_HRES           1920
#define MOCK_MAX_VRES           1080
#define MOCK_MIN_HRES           240
#define MOCK_MIN_VRES           64
#define MOCK_MAX_FRAMERATE      1000
#define MOCK_MAX_PIXELRATE      (MOCK_MAX_HRES * MOCK_MAX_VRES * MOCK_MAX_FRAMERATE)
#define MOCK_QUANTIZE_TIMING    250
#define MOCK_MAX_EXPOSURE       1000000000
#define MOCK_MIN_EXPOSURE       1000
#define MOCK_MAX_SHUTTER_ANGLE  330
#define MOCK_MAX_GAIN           24

#define MOCK_VRES_INCREMENT     2
#define MOCK_HRES_INCREMENT     32

#endif /* _MOCK_H */
