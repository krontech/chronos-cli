/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2019 Owen Kirby <oskirby@gmail.com>
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
#include <gst/video/video.h>

#include "gstneon.h"
#include <stdint.h>

GST_DEBUG_CATEGORY_STATIC (gst_neon_flip_debug);
#define GST_CAT_DEFAULT gst_neon_flip_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_METHOD,
};

static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE ("sink",
                GST_PAD_SINK,
                GST_PAD_ALWAYS,
                GST_STATIC_CAPS(GST_VIDEO_CAPS_YUV("{NV12}"))
        );

static GstStaticPadTemplate src_template =
        GST_STATIC_PAD_TEMPLATE ("src",
                GST_PAD_SRC,
                GST_PAD_ALWAYS,
                GST_STATIC_CAPS(GST_VIDEO_CAPS_YUV("{NV12}"))
        );

#define GST_TYPE_NEON_FLIP_METHOD (gst_neon_flip_method_get_type())

static const GEnumValue neon_flip_methods[] = {
  {GST_NEON_FLIP_METHOD_IDENTITY, "Identity (no rotation)", "none"},
  {GST_NEON_FLIP_METHOD_HORIZ, "Flip horizontally", "horizontal-flip"},
  {GST_NEON_FLIP_METHOD_VERT, "Flip vertically", "vertical-flip"},
  {GST_NEON_FLIP_METHOD_180, "Rotate 180 degrees", "rotate-180"},
  {0, NULL, NULL},
};

static GType
gst_neon_flip_method_get_type (void)
{
  static GType neon_flip_method_type = 0;

  if (!neon_flip_method_type) {
    neon_flip_method_type = g_enum_register_static("GstVideoFlipMethod", neon_flip_methods);
  }
  return neon_flip_method_type;
}

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_neon_flip_debug, "plugin", 0, "Template plugin");

GST_BOILERPLATE_FULL (GstNeonFlip, gst_neon_flip, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_neon_flip_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_neon_flip_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_neon_flip_transform_ip(GstBaseTransform *base, GstBuffer *outbuf);

/* GObject vmethod implementations */
static void
gst_neon_flip_base_init(gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_set_details_simple(element_class,
    "neonflip",
    "Generic",
    "NEON Magic Transformations",
    "Owen Kirby <oskirby@gmail.com>");

  gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
  gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
}

/* initialize the plugin's class */
static void
gst_neon_flip_class_init(GstNeonFlipClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_neon_flip_set_property;
  gobject_class->get_property = gst_neon_flip_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_NEON_FLIP_METHOD, GST_NEON_FLIP_METHOD_IDENTITY,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip = GST_DEBUG_FUNCPTR(gst_neon_flip_transform_ip);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_neon_flip_init (GstNeonFlip *filter, GstNeonFlipClass * klass)
{
  filter->method = GST_NEON_FLIP_METHOD_IDENTITY;
}

static void
gst_neon_flip_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstNeonFlip *filter = GST_NEON_FLIP(object);

  switch (prop_id) {
    case PROP_METHOD:
      filter->method = g_value_get_enum(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_neon_flip_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstNeonFlip *filter = GST_NEON_FLIP(object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum(value, filter->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* 180-degree rotation. */
static GstFlowReturn
gst_neon_flip_rot180(GstBuffer *buf)
{
  GstCaps *caps = GST_BUFFER_CAPS(buf);
  GstStructure *gstruct = gst_caps_get_structure(caps, 0);
  unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
  unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));

  /* Copy and and reverse the order of the luma plane. */
  uint32_t nluma = xres * yres;
  uint8_t *start_luma = GST_BUFFER_DATA(buf);
  uint8_t *end_luma = start_luma + nluma;
  asm volatile (
 	"nv12_rot180_luma:              \n"
    "   vldm %[start],  {d0-d3}     \n"
    "   vldmdb %[end]!, {d4-d7}     \n"
    "   vrev64.8 d11, d0            \n"
    "   vrev64.8 d10, d1            \n"
    "   vrev64.8 d9, d2             \n"
    "   vrev64.8 d8, d3             \n"
    "   vrev64.8 d0, d7             \n"
    "   vrev64.8 d1, d6             \n"
    "   vrev64.8 d2, d5             \n"
    "   vrev64.8 d3, d4             \n"
    "   vstmia %[start]!, {d0-d3}   \n"
    "   vstm %[end],      {d8-d11}  \n"
    "   subs %[count],%[count], #64 \n"
    "   bgt nv12_rot180_luma        \n"
    : [start]"+r"(start_luma), [end]"+r"(end_luma), [count]"+r"(nluma) :: "cc" );

  /* Copy and reverse the subsampled chroma plane. */
  uint32_t nchroma = (xres * yres) / 2;
  uint8_t *start_chroma = GST_BUFFER_DATA(buf) + (xres * yres);
  uint8_t *end_chroma = start_chroma + nchroma;
  asm volatile (
    "nv12_rot180_chroma:            \n"
    "   vldm %[start],  {d0-d3}     \n"
    "   vldmdb %[end]!, {d4-d7}     \n"
    "   vrev64.16 d11, d0           \n"
    "   vrev64.16 d10, d1           \n"
    "   vrev64.16 d9, d2            \n"
    "   vrev64.16 d8, d3            \n"
    "   vrev64.16 d0, d7            \n"
    "   vrev64.16 d1, d6            \n"
    "   vrev64.16 d2, d5            \n"
    "   vrev64.16 d3, d4            \n"
    "   vstmia %[start]!, {d0-d3}   \n"
    "   vstm %[end],      {d8-d11}  \n"
    "   subs %[count],%[count], #64 \n"
    "   bgt nv12_rot180_chroma      \n"
    : [start]"+r"(start_chroma), [end]"+r"(end_chroma), [count]"+r"(nchroma) :: "cc" );
}

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_neon_flip_transform_ip(GstBaseTransform *base, GstBuffer *outbuf)
{
  GstNeonFlip *filter = GST_NEON_FLIP(base);

  if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_TIMESTAMP(outbuf)))
    gst_object_sync_values(G_OBJECT(filter), GST_BUFFER_TIMESTAMP(outbuf));

  switch (filter->method) {
    case GST_NEON_FLIP_METHOD_180:
      gst_neon_flip_rot180(outbuf);
      break;
    
    case GST_NEON_FLIP_METHOD_IDENTITY:
    default:
      break;
  }

  return GST_FLOW_OK;
}