
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include "libvchan.h"
#include "libvchan_private.h"

static int do_read(libvchan_t *ctrl, void *data,
                   size_t min_size, size_t max_size);
static int do_write(libvchan_t *ctrl, const void *data,
                    size_t min_size, size_t max_size);

int libvchan_read(libvchan_t *ctrl, void *data, size_t size) {
    return do_read(ctrl, data, 1, size);
}

int libvchan_recv(libvchan_t *ctrl, void *data, size_t size) {
    return do_read(ctrl, data, size, size);
}

int libvchan_write(libvchan_t *ctrl, const void *data, size_t size) {
    return do_write(ctrl, data, 1, size);
}

int libvchan_send(libvchan_t *ctrl, const void *data, size_t size) {
    return do_write(ctrl, data, size, size);
}

static int do_read(libvchan_t *ctrl, void *data, size_t min_size, size_t max_size) {
    pthread_mutex_lock(&ctrl->mutex);

    size_t size = ring_filled(&ctrl->read_ring);
    while (size < min_size) {
        pthread_mutex_unlock(&ctrl->mutex);
        if (libvchan_wait(ctrl) < 0) {
            return -1;
        }
        pthread_mutex_lock(&ctrl->mutex);
        size = ring_filled(&ctrl->read_ring);
    }

    libvchan__drain_pipe(ctrl->socket_event_pipe[0]);

    if (size > max_size) {
        size = max_size;
    }

    size_t total = 0;
    while (total < size) {
        size_t fill_contig = ring_filled_contig(&ctrl->read_ring);
        if (fill_contig > size - total)
            fill_contig = size - total;
        memcpy(data + total, &ctrl->read_ring.data[ctrl->read_ring.start],
               fill_contig);
        ring_advance_start(&ctrl->read_ring, fill_contig);
        total += fill_contig;
    }

    pthread_mutex_unlock(&ctrl->mutex);

    uint8_t byte = 0;
    if (write(ctrl->user_event_pipe[1], &byte, 1) != 1) {
        perror("write user pipe");
        return -1;
    }

    return total;
}

static int do_write(libvchan_t *ctrl, const void *data,
                    size_t min_size, size_t max_size) {
    pthread_mutex_lock(&ctrl->mutex);

    size_t size = ring_available(&ctrl->write_ring);
    while (size < min_size) {
        pthread_mutex_unlock(&ctrl->mutex);
        if (libvchan_wait(ctrl) < 0) {
            return -1;
        }
        pthread_mutex_lock(&ctrl->mutex);
        size = ring_available(&ctrl->write_ring);
    }

    if (size > max_size) {
        size = max_size;
    }

    size_t total = 0;
    while (total < size) {
        size_t avail_contig = ring_available_contig(&ctrl->write_ring);

        if (avail_contig > size - total)
            avail_contig = size - total;
        memcpy(&ctrl->write_ring.data[ctrl->write_ring.end],
               data + total,
               avail_contig);
        ring_advance_end(&ctrl->write_ring, avail_contig);
        total += avail_contig;
    }

    pthread_mutex_unlock(&ctrl->mutex);

    uint8_t byte = 0;
    if (write(ctrl->user_event_pipe[1], &byte, 1) != 1) {
        perror("write user pipe");
        return -1;
    }

    return total;
}

int libvchan_wait(libvchan_t *ctrl) {
    struct pollfd fds[1];
    fds[0].fd = ctrl->socket_event_pipe[0];
    fds[0].events = POLLIN;
    if (poll(fds, 1, -1) < 0) {
        perror("poll");
        return -1;
    }

    libvchan__drain_pipe(ctrl->socket_event_pipe[0]);
    return 0;
}

int libvchan__drain_pipe(int fd) {
    uint8_t buf[16];
    if (read(fd, buf, 16) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read socket pipe");
            return -1;
        }
    }
    return 0;
}

int libvchan_data_ready(libvchan_t *ctrl) {
    pthread_mutex_lock(&ctrl->mutex);
    int result = ring_filled(&ctrl->read_ring);
    pthread_mutex_unlock(&ctrl->mutex);
    return result;
}

int libvchan_buffer_space(libvchan_t *ctrl) {
    pthread_mutex_lock(&ctrl->mutex);
    int result = ring_available(&ctrl->write_ring);
    pthread_mutex_unlock(&ctrl->mutex);
    return result;
}

int libvchan_is_open(libvchan_t *ctrl) {
    pthread_mutex_lock(&ctrl->mutex);
    int result = ctrl->state;
    pthread_mutex_unlock(&ctrl->mutex);
    return result;
}
