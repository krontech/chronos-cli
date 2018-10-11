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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "gstneon.h"

GST_DEBUG_CATEGORY_STATIC (gst_neon_debug);
#define GST_CAT_DEFAULT gst_neon_debug

/* Filter signals and args */
enum {
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_SILENT
};

/* Nothing to see here, only the identity transformation. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_neon_debug, "plugin", 0, "NEON Magic Transformations")

GST_BOILERPLATE_FULL (GstNeon, gst_neon, GstBaseTransform, GST_TYPE_BASE_TRANSFORM, DEBUG_INIT)

static void gst_neon_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_neon_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_neon_transform (GstBaseTransform *trans, GstBuffer *src, GstBuffer *dst);

/* GObject vmethod implementations */
static void
gst_neon_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "neon",
    "Generic",
    "NEON Magic Transformations",
    "Owen Kirby <oskirby@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the plugin's class */
static void
gst_neon_class_init(GstNeonClass *kclass)
{
  GObjectClass *gobject_class;
  
  gobject_class = (GObjectClass *)kclass;
  gobject_class->set_property = gst_neon_set_property;
  gobject_class->get_property = gst_neon_get_property;

  g_object_class_install_property(gobject_class, PROP_SILENT,
      g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
          TRUE, G_PARAM_READWRITE));
  
  GST_BASE_TRANSFORM_CLASS(kclass)->transform = GST_DEBUG_FUNCPTR(gst_neon_transform);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_neon_init(GstNeon * filter, GstNeonClass * gclass)
{
  filter->silent = TRUE;
}

static void
gst_neon_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstNeon *filter = GST_NEON(object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_neon_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstNeon *filter = GST_NEON(object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean(value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_neon_transform(GstBaseTransform *base, GstBuffer *src, GstBuffer *dst)
{
  GstNeon *filter = GST_NEON(base);
  unsigned int size = GST_BUFFER_SIZE(src);
  void *srcdata = GST_BUFFER_DATA(src);
  void *dstdata = GST_BUFFER_DATA(dst);

  if (filter->silent == FALSE)
    g_print ("DEBUG(%s): duping %u bytes from %p to %p\n", __func__, size, srcdata, dstdata);

  /* Copy 64-byte chunks using NEON. */
  if (size >= 64) {
    asm volatile (
        "   subs %[count],%[count], #64 \n"
      "realloc_memcpy_loop:             \n"
        "   pld [%[s], #0xc0]           \n"
        "   vldm %[s]!,{d0-d7}          \n"
        "   vstm %[d]!,{d0-d7}          \n"
        "   subs %[count],%[count], #64 \n"
        "   bge realloc_memcpy_loop     \n"
        "   add %[count],%[count], #64  \n"
        : [d]"+r"(dstdata), [s]"+r"(srcdata), [count]"+r"(size)
        :: "cc", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7" );
  }
  /* Handle leftovers */
  if (size) {
    memcpy(dstdata, srcdata, size);
  }
  return GST_FLOW_OK;
}
