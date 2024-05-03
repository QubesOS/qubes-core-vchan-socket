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

#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "ring.h"

// https://lo.calho.st/posts/black-magic-buffer/

int ring_init(struct ring *ring, size_t min_size) {
    ring->size = getpagesize();
    while (ring->size < min_size)
        ring->size <<= 1;

    ring->start = 0;
    ring->count = 0;

    ring->fd = memfd_create("ring_buffer", MFD_CLOEXEC);
    if (ring->fd < 0) {
        perror("memfd_create");
        return -1;
    }

    if (ftruncate(ring->fd, ring->size)) {
        perror("ftruncate");
        goto fail_fd;
    }

    ring->data = mmap(NULL, 2 * ring->size,
                      PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!ring->data) {
        perror("mmap");
        goto fail_mmap;
    }

    if (!mmap(ring->data, ring->size,
              PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
              ring->fd, 0)) {
        perror("mmap 1");
        goto fail_mmap;
    }
    if (!mmap(ring->data + ring->size, ring->size,
              PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
              ring->fd, 0)) {
        perror("mmap 1");
        goto fail_mmap;
    }

    return 0;

  fail_mmap:
    munmap(ring->data, ring->size * 2);
    ring->data = NULL;
  fail_fd:
    close(ring->fd);

    return -1;
}


void ring_destroy(struct ring *ring) {
    if (ring->data) {
        munmap(ring->data, ring->size * 2);
        ring->data = NULL;
        close(ring->fd);
    }
}
