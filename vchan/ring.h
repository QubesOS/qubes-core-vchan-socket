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
