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
#include <string.h>
#include <stdlib.h>

#include "segment.h"

/* Test if an address is included within the segment. */
int
video_segment_includes(struct video_segment *seg, unsigned long addr)
{
    if (seg->start <= seg->end) {
        return (addr >= seg->start) && (addr <= seg->end); /* Contiguous case. */
    }
    else {
        return (addr >= seg->start) || (addr <= seg->end); /* Wraparound case. */
    }
}

/* Test if two segments overlap. */
int
video_segment_overlap(struct video_segment *a, struct video_segment * b)
{
    if (video_segment_includes(a, b->start)) return 1;
    if (video_segment_includes(a, b->end)) return 1;
    if (video_segment_includes(b, a->start)) return 1;
    if (video_segment_includes(b, a->end)) return 1;
    /* Otherwise, the two regions cannot overlap. */
    return 0;
}

/* Lookup a video segment by logical frame number. */
struct video_segment *
video_segment_lookup(struct video_seglist *list, unsigned long position, unsigned long *address)
{
    struct video_segment *seg;
    unsigned long count;

    /* Search the recording regions for the actual frame address. */
    count = 0;
    for (seg = list->head; seg; seg = seg->next) {
        unsigned long segframe = (position - count);
        if (segframe >= seg->nframes) {
            count += seg->nframes;
            continue;
        }
        if (address) {
            unsigned long relframe = (segframe + seg->offset) % seg->nframes;
            unsigned long ramaddr = (seg->start + relframe * seg->framesz);
            if (ramaddr >= list->rec_stop) {
                ramaddr -= (list->rec_stop - list->rec_start);
            }
            *address = ramaddr;
        }
        return seg;
    }

    /* Otherwise, no such segment was found. */
    return NULL;
}

/* Remove a segment from the recording. */
void
video_segment_delete(struct video_seglist *list, struct video_segment *seg)
{
    if (seg->next) seg->next->prev = seg->prev;
    else list->tail = seg->prev;
    if (seg->prev) seg->prev->next = seg->next;
    else list->head = seg->next;
    list->totalframes -= seg->nframes;
    list->totalsegs--;
    free(seg);
}

/* Remove all segments from the recording. */
void
video_segment_flush(struct video_seglist *list)
{
    while (list->head) {
        video_segment_delete(list, list->head);
    }
    list->totalframes = 0;
    list->totalsegs = 0;
}

/* Update the total recording information. */
static void
video_seglist_update(struct video_seglist *list)
{
    struct video_segment *seg;
    unsigned long nsegs = 0;
    unsigned long nframes = 0;
    for (seg = list->head; seg; seg = seg->next) {
        seg->segno = nsegs++;
        seg->frameno = nframes;
        nframes += seg->nframes;
    }
    list->totalframes = nframes;
    list->totalsegs = nsegs;
}

/* Insert a new recording segment. */
struct video_segment *
video_segment_add(struct video_seglist *list, unsigned long start, unsigned long end, unsigned long last)
{
    struct video_segment *seg = malloc(sizeof(struct video_segment));
    if (!seg) {
        return NULL;
    }
    seg->framesz = list->framesz;
    seg->start = start;
    seg->end = end;
    seg->offset = 0;
    seg->nframes = 1;
    if (end >= start) {
        /* Contiguous case. */
        seg->nframes += (end - start) / seg->framesz;
        if (last < end) seg->offset = ((last - start) / seg->framesz) + 1;
    } else {
        /* Rollover case - region data will be split across the start and end of memory.  */
        seg->nframes += (list->rec_stop - start) / seg->framesz;
        seg->nframes += (end - list->rec_start) / seg->framesz;
        if (last < end) seg->offset = seg->nframes - (end - last) / seg->framesz;
        else if (last != end) seg->offset = ((last - start) / seg->framesz) + 1;
    }
    memset(&seg->metadata, 0, sizeof(seg->metadata));

    /* Free any segments that would overlap. */
    while (list->head) {
        if (!video_segment_overlap(seg, list->head)) break;
        video_segment_delete(list, list->head);
    }

    /* Link this segment into the end of the list. */
    seg->next = NULL;
    seg->prev = list->tail;
    if (seg->prev) {
        seg->prev->next = seg;
        list->tail = seg;
    }
    else {
        list->tail = seg;
        list->head = seg;
    }

    /* Update the total recording region size and reset back to the start. */
    video_seglist_update(list);

    /* Return the new segment to the caller. */
    return seg;
}

void
video_segments_init(struct video_seglist *list, unsigned long start, unsigned long stop, unsigned long framesz)
{
    memset(list, 0, sizeof(struct video_seglist));
    list->rec_start = start;
    list->rec_stop = stop;
    list->framesz = framesz;
}
