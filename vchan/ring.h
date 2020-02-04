#ifndef _RING_H
#define _RING_H

#include <stdlib.h>

struct ring {
    // start, end < size
    // buffer is empty when start == end, so max capacity is (size - 1)
    volatile size_t start;
    volatile size_t end;
    size_t size;
    uint8_t *data;
};

static inline int ring_init(struct ring *ring, int min_size) {
    ring->size = min_size + 1;
    ring->start = 0;
    ring->end = 0;
    ring->data = malloc(min_size);
    return ring->data ? 0 : -1;
}

static inline void ring_destroy(struct ring *ring) {
    free(ring->data);
}

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
