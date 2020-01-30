/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2013  Marek Marczykowski <marmarek@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef _LIBVCHAN_PRIVATE_H
#define _LIBVCHAN_PRIVATE_H

#include <stdint.h>
#include <pthread.h>

#include "libvchan.h"

struct ring {
    // start, end < size
    // buffer is empty when start == end, so max capacity is (size - 1)
    volatile size_t start;
    volatile size_t end;
    size_t size;
    uint8_t *data;
};

struct libvchan {
    char *socket_path;

    // Controls access to rings and state
    pthread_mutex_t mutex;

    // Notification about changes in ring (data added/removed) from user thread
    int user_event_pipe[2];

    // Notification about changes in ring (data added/removed) and connection
    // status
    int socket_event_pipe[2];

    // volatile EVTCHN state;
    struct ring read_ring;
    struct ring write_ring;
};

void *libvchan__server(void *arg);
void *libvchan__client(void *arg);
int libvchan__drain_pipe(int fd);

static inline size_t ring_available(struct ring *ring) {
    size_t start = ring->start, end = ring->end;
    if (end >= start)
        start += ring->size;
    return start - end - 1;
}

static inline size_t ring_filled(struct ring *ring) {
    size_t start = ring->start, end = ring->end;
    if (end < start)
        end += ring->size;
    return end - start;
}

static inline size_t ring_available_contig(struct ring *ring) {
    if (ring->start <= ring->end)
        return ring->size - ring->end - (ring->start == 0 ? 1 : 0);
    else
        return ring->start - ring->end - 1;
}

static inline void ring_advance_end(struct ring *ring, size_t count) {
    ring->end = (ring->end + count) % ring->size;
}

static inline size_t ring_filled_contig(struct ring *ring) {
    if (ring->start <= ring->end)
        return ring->end - ring->start;
    else
        return ring->size - ring->start;
}

static inline void ring_advance_start(struct ring *ring, size_t count) {
    ring->start = (ring->start + count) % ring->size;
}

#endif
