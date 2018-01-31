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
#ifndef __PIPELINE_H
#define __PIPELINE_H

#include <gst/gst.h>

#define SCREENCAP_PATH  "/tmp/cam-screencap.jpg"

struct display_config {
    unsigned long hres;
    unsigned long vres;
    unsigned long xoff;
    unsigned long yoff;
};

/* Allocate pipeline segments, returning the first element to be linked. */
GstPad *cam_lcd_sink(GstElement *pipeline, unsigned long hres, unsigned long vres, const struct display_config *config);
GstPad *cam_hdmi_sink(GstElement *pipeline, unsigned long hres, unsigned long vres);
GstPad *cam_screencap(GstElement *pipeline);

#endif /* __PIPELINE */
