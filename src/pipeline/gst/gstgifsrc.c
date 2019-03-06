/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2019 Owen Kirby <oskirby@gmail.com>
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
#include <unistd.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "gifdec.h"
#include "gstgifsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_gif_src_debug);
#define GST_CAT_DEFAULT gst_gif_src_debug

/* Filter signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION
};

static void gst_gif_src_finalize(GObject * object);
static gboolean gst_gif_src_is_seekable(GstBaseSrc *basesrc);
static GstFlowReturn gst_gif_src_create(GstPushSrc *basesrc, GstBuffer **ret);
static gboolean gst_gif_src_start(GstBaseSrc *basesrc);
static gboolean gst_gif_src_stop(GstBaseSrc *basesrc);

static GstCaps *gst_gif_src_getcaps(GstBaseSrc *bsrc);
static gboolean gst_gif_src_setcaps(GstBaseSrc *bsrc, GstCaps *caps);

static void gst_gif_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gif_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(GST_VIDEO_CAPS_RGB));

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gif_src_debug, "gifsrc", 0, "GIF source element")

GST_BOILERPLATE_FULL (GstGifSrc, gst_gif_src, GstPushSrc, GST_TYPE_PUSH_SRC, DEBUG_INIT);

static void
gst_gif_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple(gstelement_class,
    "GIF Source",
    "Source/File",
    "Read frames from an animated GIF",
    "Owen Kirby <oskirby@gmail.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
}

static void
gst_gif_src_class_init(GstGifSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->set_property = gst_gif_src_set_property;
  gobject_class->get_property = gst_gif_src_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to read", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gstbasesrc_class->get_caps = gst_gif_src_getcaps;
  gstbasesrc_class->set_caps = gst_gif_src_setcaps;
  gstbasesrc_class->is_seekable = gst_gif_src_is_seekable;
  gstbasesrc_class->start = gst_gif_src_start;
  gstbasesrc_class->stop = gst_gif_src_stop;

  gobject_class->finalize = gst_gif_src_finalize;

  gstpushsrc_class->create = gst_gif_src_create;
}

static void
gst_gif_src_init (GstGifSrc *src, GstGifSrcClass *g_class)
{
  GstBaseSrc *basesrc = GST_BASE_SRC(src);

  src->filename = NULL;
  src->gif = NULL;

  /* We operate in time */
  gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);
  gst_base_src_set_live(GST_BASE_SRC(src), TRUE);
}

static void
gst_gif_src_finalize (GObject * object)
{
  GstGifSrc *src = GST_GIF_SRC (object);

  g_free (src->filename);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gif_src_set_location (GstGifSrc * src, const gchar * location)
{
  GstState state;

  /* the element must be stopped in order to do this */
  GST_OBJECT_LOCK (src);
  state = GST_STATE (src);
  if (state != GST_STATE_READY && state != GST_STATE_NULL)
    goto wrong_state;
  GST_OBJECT_UNLOCK (src);

  g_free (src->filename);

  /* clear the filename if we get a NULL (is that possible?) */
  if (location == NULL) {
    src->filename = NULL;
  } else {
    /* we store the filename as received by the application. On Windows this
     * should be UTF8 */
    src->filename = g_strdup (location);
    GST_INFO ("filename : %s", src->filename);
  }
  g_object_notify (G_OBJECT (src), "location");

  return TRUE;

  /* ERROR */
wrong_state:
  {
    g_warning ("Changing the `location' property on gifsrc when a file is "
        "open is not supported.");
    GST_OBJECT_UNLOCK (src);
    return FALSE;
  }
}

static void
gst_gif_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGifSrc *src;

  g_return_if_fail (GST_IS_GIF_SRC (object));

  src = GST_GIF_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_gif_src_set_location (src, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gif_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstGifSrc *src;

  g_return_if_fail (GST_IS_GIF_SRC (object));

  src = GST_GIF_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Pretend to be a live source. */
static gboolean
gst_gif_src_is_seekable(GstBaseSrc *basesrc)
{
  return FALSE;
}

/* open the file and parse it, necessary to go to READY state */
static gboolean
gst_gif_src_start(GstBaseSrc * basesrc)
{
  GstGifSrc *src = GST_GIF_SRC(basesrc);
  guchar *bgcolor;
  guint i;

  if (src->filename == NULL || src->filename[0] == '\0')
    goto no_filename;

  GST_INFO_OBJECT(src, "opening file %s", src->filename);

  /* open the GIF file */
  src->gif = gd_open_gif(src->filename);
  if (!src->gif) {
    goto open_failed;
  }

  /* Allocate memory for the frame buffer. */
  src->timestamp = 0;
  src->iframe_delay_usec = 0;
  src->size = src->gif->width * src->gif->height * 3;
  src->frame = g_malloc(src->size);
  if (!src->frame) {
    goto malloc_failed;
  }

  /* Load the background color. */
  bgcolor = &src->gif->gct.colors[src->gif->bgindex * 3];
  for (i = 0; i < src->size; i += 3) {
      src->frame[i + 0] = bgcolor[0];
      src->frame[i + 1] = bgcolor[1];
      src->frame[i + 2] = bgcolor[2];
  }

  //gst_base_src_set_dynamic_size(basesrc, TRUE); /* TODO: ??? Not present in older GST? */

  return TRUE;

  /* ERROR */
no_filename:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("No file name specified for reading."), (NULL));
    return FALSE;
  }
open_failed:
  {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
            ("No such file \"%s\"", src->filename));
        break;
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
            ("Could not open file \"%s\" for reading.", src->filename), (NULL));
        break;
    }
    return FALSE;
  }
malloc_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Memory allocation failed."), (NULL));
    return FALSE;
  }
}

/* close the file */
static gboolean
gst_gif_src_stop (GstBaseSrc * basesrc)
{
  GstGifSrc *src = GST_GIF_SRC (basesrc);

  /* close the file */
  g_free (src->frame);
  gd_close_gif(src->gif);
  
  src->frame = NULL;
  src->gif = NULL;
  return TRUE;
}

static gboolean
gst_gif_src_setcaps(GstBaseSrc *bsrc, GstCaps *caps)
{
#if 0
  gboolean res;
  gint width, height, rate_denominator, rate_numerator;
  struct fourcc_list_struct *fourcc;
  GstVideoTestSrc *videotestsrc;
  GstVideoTestSrcColorSpec color_spec;

  videotestsrc = GST_VIDEO_TEST_SRC (bsrc);

  res = gst_video_test_src_parse_caps (caps, &width, &height,
      &rate_numerator, &rate_denominator, &fourcc, &color_spec);
  if (res) {
    /* looks ok here */
    videotestsrc->fourcc = fourcc;
    videotestsrc->width = width;
    videotestsrc->height = height;
    videotestsrc->rate_numerator = rate_numerator;
    videotestsrc->rate_denominator = rate_denominator;
    videotestsrc->bpp = videotestsrc->fourcc->bitspp;
    videotestsrc->color_spec = color_spec;

    GST_DEBUG_OBJECT (videotestsrc, "size %dx%d, %d/%d fps",
        videotestsrc->width, videotestsrc->height,
        videotestsrc->rate_numerator, videotestsrc->rate_denominator);
  }
  return res;
#endif
}

static GstCaps *
gst_gif_src_getcaps(GstBaseSrc *bsrc)
{
  GstGifSrc *src = GST_GIF_SRC(bsrc);
  if (!src->gif) {
    GST_DEBUG_OBJECT (src, "file not open, using template caps");
    return NULL; /* base class will get template caps for us */
  }
  return gst_caps_new_simple("video/x-raw-rgb",
    "width", G_TYPE_INT, src->gif->width,
    "height", G_TYPE_INT, src->gif->height,
    NULL);
}

/* Request the next frame from the GIF */
static GstFlowReturn
gst_gif_src_create(GstPushSrc *psrc, GstBuffer **buffer)
{
  GstGifSrc *src = GST_GIF_SRC(psrc);
  GstBuffer *buf;
  int ret;

  GST_INFO_OBJECT(src, "allocating frame");

  buf = gst_buffer_try_new_and_alloc(src->size);
  if (G_UNLIKELY (buf == NULL && src->size > 0)) {
    GST_ERROR_OBJECT(src, "Failed to allocate %u bytes", src->size);
    return GST_FLOW_ERROR;
  }

  GST_INFO_OBJECT(src, "rendering frame");

  /* HACK: Delay for the inter-frame time. */
  usleep(src->iframe_delay_usec);

  do {
    ret = gd_get_frame(src->gif);
    if (ret < 0) {
      GST_ERROR_OBJECT(src, "GIF decoding failed");
      gst_buffer_unref(buf);
      return GST_FLOW_ERROR;
    }
    gd_render_frame(src->gif, src->frame);
  } while ((src->gif->gce.delay == 0) && (ret != 0));
  GST_BUFFER_TIMESTAMP(buf) = src->timestamp;
  GST_BUFFER_DURATION(buf) = (src->gif->gce.delay * GST_SECOND) / 100;
  memcpy(GST_BUFFER_DATA(buf), src->frame, src->size);

  src->iframe_delay_usec = src->gif->gce.delay * 10000;
  src->timestamp += (src->gif->gce.delay * GST_SECOND) / 100;
  GST_INFO_OBJECT(src, "returning frame (duration=%d)", src->gif->gce.delay);

  /* TODO: Technically a GIF can end, we should respect src->gif->loop_count */
  if (ret == 0) {
    gd_rewind(src->gif);
  }

  *buffer = buf;
  return GST_FLOW_OK;
}
