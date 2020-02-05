/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2020  Paweł Marczewski  <pawel@invisiblethingslab.com>
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

#ifndef _RING_H
#define _RING_H

#include <stdint.h>

struct ring {
    volatile size_t start;
    volatile size_t count;

    // Will always be a power of 2
    size_t size;

    // "Magic buffer trick": the buffer is mapped twice, so that ring_head()
    // and ring_tail() will point to a contiguous chunk of memory.
    uint8_t *data;
    int fd;
};

int ring_init(struct ring *ring, size_t min_size);
void ring_destroy(struct ring *ring);

inline size_t ring_available(struct ring *ring) {
    return ring->size - ring->count;
}

inline size_t ring_filled(struct ring *ring) {
    return ring->count;
}

inline uint8_t *ring_head(struct ring *ring) {
    return ring->data + ring->start;
}

inline uint8_t *ring_tail(struct ring *ring) {
    return ring->data + ((ring->start + ring->count) & (ring->size - 1));
}

inline void ring_advance_head(struct ring *ring, size_t count) {
    ring->start = (ring->start + count) & (ring->size - 1);
    ring->count -= count;
}

inline void ring_advance_tail(struct ring *ring, size_t count) {
    ring->count += count;
}

#endif
