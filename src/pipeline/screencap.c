
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <gst/gst.h>
#include <jpeglib.h>

#include "pipeline.h"

struct jpeg_err_jmpbuf {
    struct jpeg_error_mgr pub;
    jmp_buf jmp_abort;
};

static void
jpeg_error_abort(j_common_ptr cinfo)
{
    struct jpeg_err_jmpbuf *err = (struct jpeg_err_jmpbuf *)cinfo->err;
    longjmp(err->jmp_abort, 1);
}

/* Convert NV12 semiplanar chroma data into YUV planar data. */
static inline void
jpeg_copy_chroma_nv12(JSAMPLE *u, JSAMPLE *v, JSAMPLE *chroma, unsigned int num)
{
    unsigned int i;
    for (i = 0; i < num; i++) {
        u[i] = chroma[i & ~1];
        v[i] = chroma[i | 1];
    }
}

static gboolean
buffer_framegrab(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    GstCaps *caps = GST_BUFFER_CAPS(buffer);
    GstStructure *gstruct;
    struct jpeg_compress_struct cinfo;
    struct jpeg_err_jmpbuf jerr;
    /* Planar data to the JPEG encoder. */
    JSAMPROW yrow[DCTSIZE];
    JSAMPROW urow[DCTSIZE];
    JSAMPROW vrow[DCTSIZE];
    JSAMPARRAY data[3] = { yrow, urow, vrow };
    JSAMPLE *planar;
    /* Input semiplanar data from gstreamer. */
    unsigned int i, row;
    FILE *fp;

    /* Check if the FIFO has been opened. */
    int fd = open(SCREENCAP_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return TRUE;
    }
    fp = fdopen(fd, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open frame grabber output pipe: %s\n", strerror(errno));
        close(fd);
        return TRUE;
    }
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    /* Parse the frame's resolution from the buffer's caps. */
    gstruct = gst_caps_get_structure(caps, 0);
    cinfo.image_width = g_value_get_int(gst_structure_get_value(gstruct, "width"));
    cinfo.image_height = g_value_get_int(gst_structure_get_value(gstruct, "height"));
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;

    /* Allocate working memory for the JPEG encoder's chroma data. */
    planar = malloc(sizeof(JSAMPLE) * cinfo.image_width * DCTSIZE);
    if (!planar) {
        fprintf(stderr, "Failed to allocate working memory for JPEG encoder: %s\n", strerror(errno));
        fclose(fp);
        close(fd);
        return TRUE;
    }
    
    /* Point even and odd row pairs to the same memory to upscale the vertical chroma. */
    for (i = 0; i < DCTSIZE; i++) {
        urow[i] = planar + cinfo.image_width * (i & ~1);
        vrow[i] = planar + cinfo.image_width * (i | 1);
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_abort;
    if (setjmp(jerr.jmp_abort) == 0) {
        JSAMPLE *luma = buffer->data;
        JSAMPLE *chroma = luma + (cinfo.image_width * cinfo.image_height);

        /* Prepare for raw encoding of NV12 data. */
        jpeg_set_defaults(&cinfo);
        cinfo.dct_method = JDCT_IFAST;
        cinfo.raw_data_in = TRUE;
        cinfo.comp_info[0].h_samp_factor = 1;
        cinfo.comp_info[0].v_samp_factor = 1;
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;

        jpeg_set_quality(&cinfo, 70, TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        for (row = 0; row < cinfo.image_height; row += DCTSIZE) {
            for (i = 0; i < DCTSIZE; i++) {
                if (!(i & 1)) {
                    jpeg_copy_chroma_nv12(urow[i], vrow[i], chroma, cinfo.image_width);
                    if ((row + i) < cinfo.image_height) chroma += cinfo.image_width;
                }
                yrow[i] = luma;
                if ((row + i) < cinfo.image_height) luma += cinfo.image_width;
            }
            jpeg_write_raw_data(&cinfo, data, DCTSIZE);
        }
        jpeg_finish_compress(&cinfo);
    }
    jpeg_destroy_compress(&cinfo);
    free(planar);
    fclose(fp);
    close(fd);
    return TRUE;
}

GstPad *
cam_screencap(struct pipeline_state *state)
{
    GstElement *queue, *sink;
    GstPad *pad;

    /* Create a queue followed by a fakesink to throw away the frames. */
    queue = gst_element_factory_make("queue",       "jpegqueue");
    sink =  gst_element_factory_make("fakesink",    "jpegsink");
    if (!queue || !sink) {
        return NULL;
    }
    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);

    /* Grab frames from the queue */
    pad = gst_element_get_static_pad(queue, "src");
    gst_pad_add_buffer_probe(pad, G_CALLBACK(buffer_framegrab), NULL);
    gst_object_unref(pad);

    return gst_element_get_static_pad(queue, "sink");
}
