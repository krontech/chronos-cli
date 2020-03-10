/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2020 Owen Kirby <oskirby@gmail.com>
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

GST_DEBUG_CATEGORY_STATIC (gst_neon_crop_debug);
#define GST_CAT_DEFAULT gst_neon_crop_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_FLIP,
  PROP_TOP,
  PROP_BOTTOM,
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

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_neon_crop_debug, "plugin", 0, "Template plugin");

GST_BOILERPLATE_FULL (GstNeonCrop, gst_neon_crop, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

/* Caps negotiation */
static void gst_neon_crop_recalc_transform(GstNeonCrop *crop);
static GstCaps *gst_neon_crop_transform_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *from);
static gboolean gst_neon_crop_set_caps(GstBaseTransform *base, GstCaps *in, GstCaps *out);
static gboolean gst_neon_crop_get_unit_size(GstBaseTransform *base, GstCaps *caps, guint *size);

static void gst_neon_crop_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_neon_crop_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_neon_crop_transform(GstBaseTransform *base, GstBuffer *src, GstBuffer *dst);

/* GObject vmethod implementations */
static void
gst_neon_crop_base_init(gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_set_details_simple(element_class,
    "neoncrop",
    "Generic",
    "NEON Magic Transformations",
    "Owen Kirby <oskirby@gmail.com>");

  gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
  gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
}

/* initialize the plugin's class */
static void
gst_neon_crop_class_init(GstNeonCropClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_neon_crop_set_property;
  gobject_class->get_property = gst_neon_crop_get_property;

  g_object_class_install_property (gobject_class, PROP_FLIP,
      g_param_spec_boolean ("flip", "Flip", "Flip image 180 degrees during crop.",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject_class, PROP_TOP,
      g_param_spec_int ("top", "Top", "Pixels to crop from the top (<0 to add border)",
          G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BOTTOM,
      g_param_spec_int ("bottom", "Bottom", "Pixels to crop from the bottom (<0 to add border)",
          G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  GST_BASE_TRANSFORM_CLASS (klass)->transform = GST_DEBUG_FUNCPTR(gst_neon_crop_transform);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps = GST_DEBUG_FUNCPTR(gst_neon_crop_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = GST_DEBUG_FUNCPTR(gst_neon_crop_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = GST_DEBUG_FUNCPTR(gst_neon_crop_get_unit_size);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_neon_crop_init(GstNeonCrop *filter, GstNeonCropClass * klass)
{
  filter->flip = FALSE;
  filter->top = 0;
  filter->bottom = 0;
}

static void
gst_neon_crop_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstNeonCrop *filter = GST_NEON_CROP(object);

  switch (prop_id) {
    case PROP_FLIP:
      filter->flip = g_value_get_boolean(value);
      break;
    case PROP_TOP:
      filter->top = g_value_get_int(value);
      break;
    case PROP_BOTTOM:
      filter->bottom = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
  gst_neon_crop_recalc_transform(filter);
}

static void
gst_neon_crop_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstNeonCrop *filter = GST_NEON_CROP(object);

  switch (prop_id) {
    case PROP_FLIP:
      g_value_set_boolean(value, filter->flip);
      break;
    case PROP_TOP:
      g_value_set_int(value, filter->top);
      break;
    case PROP_BOTTOM:
      g_value_set_int(value, filter->bottom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_neon_crop_transform_dimension(const GValue *src, gint delta, GValue *dest)
{
  g_value_init(dest, G_VALUE_TYPE(src));
  if (G_VALUE_HOLDS_INT(src)) {
    gint ival = g_value_get_int(src) + delta;
    if (ival < 0) return FALSE;
    g_value_set_int(dest, ival);
  }
  else if (GST_VALUE_HOLDS_INT_RANGE(src)) {
    gint imin = gst_value_get_int_range_min(src) + delta;
    gint imax = gst_value_get_int_range_max(src);
    if (imin < 0) imin = 0;

    /* Some care and handling is necessary in case of rollovers */
    if (delta < 0) imax + delta;
    else if (imax > (G_MAXINT-delta)) imax = G_MAXINT;
    else imax = imax + delta;
    if (imax < 0) return FALSE;

    gst_value_set_int_range(dest, imin, imax);
  }
  else {
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_neon_crop_transform_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *from)
{
  GstNeonCrop *crop = GST_NEON_CROP(base);
  GstStructure *gstruct;
  GstPad *other = (direction == GST_PAD_SINK) ? base->srcpad : base->sinkpad;
  const GstCaps *templ;
  const GValue *gval;
  gint dx = 0;
  gint dy = 0;
  GValue yresval = { 0 };
  GstCaps *to, *ret;

  /* Duplicate the caps */
  to = gst_caps_copy(from);
  gst_caps_truncate(to);
  gstruct = gst_caps_get_structure(to, 0);

  /* Adjust the resolution as needed */
  if (direction == GST_PAD_SINK) {
    dy -= crop->top;
    dy -= crop->bottom;
  }
  else {
    dy += crop->top;
    dy += crop->bottom;
  }

  gval = gst_structure_get_value(gstruct, "height");
  if (!gst_neon_crop_transform_dimension(gval, dy, &yresval)) {
    GST_WARNING_OBJECT(crop, "could not transform height value with dy=%d and caps=%" GST_PTR_FORMAT, dy, gstruct);
    gst_caps_unref(to);
    to = gst_caps_new_empty();
    return to;
  }
  gst_structure_set_value(gstruct, "height", &yresval);
  g_value_unset(&yresval);

  /* Filter the resulting caps against the template.  */
  templ = gst_pad_get_pad_template_caps(other);
  ret = gst_caps_intersect(to, templ);
  gst_caps_unref(to);

  GST_DEBUG_OBJECT (crop, "direction %d, transformed %" GST_PTR_FORMAT
      " to %" GST_PTR_FORMAT, direction, from, ret);

  return ret;
}

static gboolean
gst_neon_crop_get_unit_size(GstBaseTransform *base, GstCaps *caps, guint *size)
{
  GstStructure *gstruct = gst_caps_get_structure(caps, 0);
  unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
  unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));

  g_assert (size);
  *size = (xres * yres * 12) / 8; /* NV12 is 12-bits per pixel */

  return TRUE;
}

static void
gst_neon_crop_recalc_transform(GstNeonCrop *crop)
{
  if ((crop->top == 0) && (crop->bottom == 0)) {
    GST_INFO_OBJECT(crop, "Using Passthrough Mode");
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM_CAST(crop), TRUE);
  } else {
    GST_INFO_OBJECT(crop, "Using Non-Passthrough Mode");
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM_CAST(crop), FALSE);
  }
}

static gboolean
gst_neon_crop_set_caps(GstBaseTransform *base, GstCaps *in, GstCaps *out)
{
  gst_neon_crop_recalc_transform(GST_NEON_CROP(base));
}

static void *fill_luma(void *dst, uint8_t val, unsigned long size)
{
  /* Fill 16-byte chunks using NEON. */
  asm volatile (
    "   vdup.8 q0, %[v]             \n"
    "   subs %[count],%[count], #16 \n"
    "neon_crop_memset%=:            \n"
    "   vstm %[d]!,{q0}             \n"
    "   subs %[count],%[count], #16 \n"
    "   bge neon_crop_memset%=      \n"
    "   add %[count],%[count], #16  \n"
    : [d]"+r"(dst), [count]"+r"(size) : [v]"r"(val) : "cc" );
  return dst;
}

static void copy_data(void *dst, const void *src, unsigned long size)
{
  /* Truncate the size to a multiple of the NEON word width */
  size &= ~0x7UL;
  asm volatile (
      /* First loop copies as wide of a burst as we can fit */
      "   subs %[count],%[count], #64 \n"
      "neon_crop_memcpy_large:        \n"
      "   pld [%[s], #0xc0]           \n"
      "   vldm %[s]!,{d0-d7}          \n"
      "   vstm %[d]!,{d0-d7}          \n"
      "   subs %[count],%[count], #64 \n"
      "   bge neon_crop_memcpy_large  \n"
      "   add %[count],%[count], #64  \n"
      /* Second loop copies as narrow of a burst as we can */
      "neon_crop_memcpy_small:        \n"
      "   vldm %[s]!,{d0}             \n"
      "   vstm %[d]!,{d0}             \n"
      "   subs %[count],%[count], #8  \n"
      "   bge neon_crop_memcpy_small  \n"
      : [d]"+r"(dst), [s]"+r"(src), [count]"+r"(size)
      :: "cc", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7" );
}

static void brev_data(void *dst, const void *src, unsigned long size)
{
  const uint8_t *end = src + size;
  asm volatile (
 	"neon_crop_brev:                  \n"
    "   vldmdb %[end]!, {d0-d1}     \n"
    "   vrev64.8 d3, d0             \n"
    "   vrev64.8 d2, d1             \n"
    "   vstmia %[dst]!, {d2-d3}     \n"
    "   cmp %[end], %[start]        \n"
    "   bgt neon_crop_brev          \n"
    : [dst]"+r"(dst), [end]"+r"(end) : [start]"r"(src) : "cc" );
}

static void brev_chroma(void *dst, const void *src, unsigned long size)
{
  const uint8_t *end = src + size;
  asm volatile (
    "neon_crop_brev_chroma:         \n"
    "   vldmdb %[end]!, {d0-d1}     \n"
    "   vrev64.16 d3, d0            \n"
    "   vrev64.16 d2, d1            \n"
    "   vstmia %[dst]!, {d2-d3}     \n"
    "   cmp %[end], %[start]        \n"
    "   bgt neon_crop_brev_chroma   \n"
    : [dst]"+r"(dst), [end]"+r"(end) : [start]"r"(src) : "cc" );
}

static GstFlowReturn
gst_neon_crop_transform(GstBaseTransform *base, GstBuffer *src, GstBuffer *dst)
{
  GstNeonCrop *crop = GST_NEON_CROP(base);
  GstCaps *caps = GST_BUFFER_CAPS(src);
  GstStructure *gstruct = gst_caps_get_structure(caps, 0);
  unsigned long xres = g_value_get_int(gst_structure_get_value(gstruct, "width"));
  unsigned long yres = g_value_get_int(gst_structure_get_value(gstruct, "height"));
  char *srcluma = GST_BUFFER_DATA(src);
  char *dstluma = GST_BUFFER_DATA(dst);
  char *srcchroma = srcluma + (xres * yres);
  char *dstchroma = dstluma + xres * (yres - crop->top - crop->bottom);

  /* Apply top padding. */
  if (crop->top < 0) {
      unsigned int top = -crop->top;
      /* Luma */
      dstluma = fill_luma(dstluma, 0, xres * top);
      /* Chroma */
      dstchroma = fill_luma(dstchroma, 0, xres * top / 2);
  }
  /* Apply top cropping. */
  else {
      yres -= crop->top;
      srcluma += xres * crop->top;
      srcchroma += (xres * crop->top) / 2;
  }
  /* Apply bottom cropping. */
  if (crop->bottom > 0) {
      yres -= crop->bottom;
  }

  /* Copy the luma plane */
  if (crop->flip) {
    brev_data(dstluma, srcluma, xres * yres);
  } else {
    copy_data(dstluma, srcluma, xres * yres);
  }
  dstluma += xres * yres;

  /* Copy the chroma plane */
  if (crop->flip) {
    brev_chroma(dstchroma, srcchroma, xres * yres / 2);
  } else {
    copy_data(dstchroma, srcchroma, xres * yres / 2);
  }
  dstchroma += xres * yres / 2;

  /* Apply bottom padding. */
  if (crop->bottom < 0) {
      unsigned int bottom = -crop->bottom;
      /* Luma */
      dstluma = fill_luma(dstluma, 0, xres * bottom);
      /* Chroma */
      dstchroma = fill_luma(dstchroma, 0, xres * bottom / 2);
  }

  return GST_FLOW_OK;
}
