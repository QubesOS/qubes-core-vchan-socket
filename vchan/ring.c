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
    ring->data = malloc(min_size);

    ring->fd = memfd_create("ring_buffer", 0);
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
