/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2018 Owen Kirby <oskirby@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_NEON_H__
#define __GST_NEON_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

/*=========================================================
 * NEON Accelerated Buffer Deepcopy Element
 *=========================================================
 */
#define GST_TYPE_NEON         (gst_neon_get_type())
#define GST_NEON(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NEON,GstNeon))
#define GST_NEON_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NEON,GstNeonClass))
#define GST_IS_NEON(obj)      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NEON))
#define GST_IS_NEON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NEON))

typedef struct _GstNeon      GstNeon;
typedef struct _GstNeonClass GstNeonClass;

struct _GstNeon {
  GstBaseTransform element;
  GstPad *sinkpad, *srcpad;
  gboolean silent;
};

struct _GstNeonClass {
  GstBaseTransformClass parent_class;
};

GType gst_neon_get_type (void);

/*=========================================================
 * NEON Accelerated Video Flip Element
 *=========================================================
 */
typedef enum {
  GST_NEON_FLIP_METHOD_IDENTITY,
  GST_NEON_FLIP_METHOD_HORIZ,
  GST_NEON_FLIP_METHOD_VERT,
  GST_NEON_FLIP_METHOD_180
} GstNeonFlipMethod;

#define GST_TYPE_NEON_FLIP \
  (gst_neon_flip_get_type())
#define GST_NEON_FLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NEON_FLIP,GstNeonFlip))
#define GST_NEON_FLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NEON_FLIP,GstNeonFlipClass))
#define GST_IS_NEON_FLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NEON_FLIP))
#define GST_IS_NEON_FLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NEON_FLIP))

typedef struct _GstNeonFlip      GstNeonFlip;
typedef struct _GstNeonFlipClass GstNeonFlipClass;

struct _GstNeonFlip {
  GstBaseTransform element;

  GstNeonFlipMethod method;
};

struct _GstNeonFlipClass {
  GstBaseTransformClass parent_class;
};

GType gst_neon_flip_get_type (void);

/*=========================================================
 * NEON Accelerated Video Crop/Pad Element
 *=========================================================
 */
#define GST_TYPE_NEON_CROP \
  (gst_neon_crop_get_type())
#define GST_NEON_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NEON_CROP,GstNeonCrop))
#define GST_NEON_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NEON_CROP,GstNeonCropClass))
#define GST_IS_NEON_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NEON_CROP))
#define GST_IS_NEON_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NEON_CROP))

typedef struct _GstNeonCrop      GstNeonCrop;
typedef struct _GstNeonCropClass GstNeonCropClass;

struct _GstNeonCrop {
  GstBaseTransform element;

  gboolean flip;
  gint top;
  gint bottom;
};

struct _GstNeonCropClass {
  GstBaseTransformClass parent_class;
};

GType gst_neon_crop_get_type (void);

G_END_DECLS

#endif /* __GST_NEON_H__ */
