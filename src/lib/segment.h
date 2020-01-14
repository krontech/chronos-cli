/****************************************************************************
 *  Copyright (C) 2019 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#ifndef _SEGMENT_H
#define _SEGMENT_H

/* Video Segment structure. */
struct video_segment {
    struct video_segment *next;
    struct video_segment *prev;
    
    /* Size and starting address of the recorded frames. */
    unsigned long   start;
    unsigned long   end;
    unsigned long   framesz;
    unsigned long   nframes;    /* Number of frames captured in this segment. */
    unsigned long   offset;     /* Offset (in frames) into the segment where recording starts. */

    /* Position with the overall recording. */
    unsigned long   segno;
    unsigned long   frameno;

    /* Some frame metadata captured along with the frames. */
    struct {
        unsigned long exposure;
        unsigned long interval;
        unsigned long timebase;
    } metadata;
};

/* Combined video recording from multiple segments. */
struct video_seglist {
    /* Individual segments stored in a doubly-linked list. */
    struct video_segment *head;
    struct video_segment *tail;

    /* Recording region information */
    unsigned long   rec_start;
    unsigned long   rec_stop;
    unsigned long   framesz;

    /* Some total recording info. */
    unsigned long   totalsegs;      /* Total number of recording segments captured. */
    unsigned long   totalframes;    /* Total number of frames when in playback mode. */
};

int video_segment_includes(struct video_segment *seg, unsigned long address);
int video_segment_overlap(struct video_segment *a, struct video_segment *b);
struct video_segment *video_segment_lookup(struct video_seglist *list, unsigned long position, unsigned long *address);

void video_segment_delete(struct video_seglist *list, struct video_segment *seg);
void video_segment_flush(struct video_seglist *list);
struct video_segment *video_segment_add(struct video_seglist *list, unsigned long start, unsigned long end, unsigned long last);

void video_segments_init(struct video_seglist *list, unsigned long start, unsigned long stop, unsigned long framesz);

#endif /* _SEGMENT_H */
