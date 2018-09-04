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
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <gst/gst.h>
#include <sys/mman.h>

#include "pipeline.h"
#include "utils.h"

static gboolean
raw_probe(GstPad *pad, GstBuffer *buffer, gpointer cbdata)
{
    struct pipeline_state *state = cbdata;
    const uint8_t *data = GST_BUFFER_DATA(buffer);
    int size = GST_BUFFER_SIZE(buffer);

    /* Allocate working memory. */
    uint8_t *kpage = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (kpage == MAP_FAILED) {
        fprintf(stderr, "mmap() failed: %s\n", strerror(errno));
        return TRUE;
    }

    /* Use NEON to copy into the working buffer, and then write to disk. */
    while (size > 4096) {
        memcpy_neon(kpage, data, 4096);
        write(state->write_fd, kpage, 4096);
        data += 4096;
        size -= 4096;
    }
    if (size) {
        memcpy_neon(kpage, data, size);
        write(state->write_fd, kpage, size);
    }

    /* Cleanup */
    munmap(kpage, 4096);
    return TRUE;
}

GstPad *
cam_raw_sink(struct pipeline_state *state, struct pipeline_args *args)
{
    GstElement *queue, *sink;
    GstPad *pad;
    int flags = O_RDWR | O_CREAT | O_TRUNC;
#if defined(O_LARGEFILE)
    flags |= O_LARGEFILE;
#elif defined(__O_LARGEFILE)
    flags |= __O_LARGEFILE;
#endif

    /* Open the file for writing. */
	state->write_fd = open(args->filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (state->write_fd < 0) {
        fprintf(stderr, "Unable to open %s for writing (%s)\n", args->filename, strerror(errno));
        return NULL;
	}

    /* Allocate our segment of the video pipeline. */
    queue =		gst_element_factory_make("queue",		    "raw-queue");
    sink =		gst_element_factory_make("fakesink",		"file-sink");
    if (!queue || !sink) {
        close(state->write_fd);
        return NULL;
    }
    /* Configure the file sink */
	pad = gst_element_get_static_pad(queue, "src");
	gst_pad_add_buffer_probe(pad, G_CALLBACK(raw_probe), state);
	gst_object_unref(pad);

    /* Return the first element of our segment to link with */
    gst_bin_add_many(GST_BIN(state->pipeline), queue, sink, NULL);
    gst_element_link_many(queue, sink, NULL);
    return gst_element_get_static_pad(queue, "sink");
} /* cam_raw_sink */
