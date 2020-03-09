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

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <assert.h>
#include <sys/socket.h>
#include <fcntl.h>
#include "libvchan.h"
#include "libvchan_private.h"

static int do_read(libvchan_t *ctrl, void *data,
                   size_t min_size, size_t max_size);
static int do_write(libvchan_t *ctrl, const void *data,
                    size_t min_size, size_t max_size);
static int read_pending(libvchan_t *ctrl);
static int wait_for_read(libvchan_t *ctrl);
static int wait_for_write(libvchan_t *ctrl);
static int wait_for_connection(libvchan_t *ctrl);
static void close_socket(libvchan_t *ctrl);

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
    size_t size = ring_filled(&ctrl->read_ring);
    while (size < min_size) {
        if (libvchan_wait(ctrl) < 0) {
            return -1;
        }
        size = ring_filled(&ctrl->read_ring);
    }

    if (size > max_size)
        size = max_size;

    memcpy(data, ring_head(&ctrl->read_ring), size);
    ring_advance_head(&ctrl->read_ring, size);

    return size;
}

static int do_write(libvchan_t *ctrl, const void *data,
                    size_t min_size, size_t max_size) {
    if (max_size == 0)
        return 0;

    size_t size = 0;

    for (;;) {
        if (ctrl->socket_fd >= 0) {
            int ret = write(ctrl->socket_fd, data + size, max_size - size);
            if (ret < 0) {
                if (errno == EAGAIN)
                    ret = 0;
                else if (errno == EPIPE || errno == ECONNRESET) {
                    close_socket(ctrl);
                    ret = 0;
                } else {
                    perror("write");
                    return -1;
                }
            }
            size += ret;
            if (size >= min_size)
                break;

            wait_for_write(ctrl);
            if (ctrl->socket_fd < 0)
                continue;
        } else
            libvchan_wait(ctrl);
    }
    return size;
}

/*
 * Wait for state to change: either new data to read, or connect/disconnect.
 *
 * If we waited on libvchan_fd_for_select(), this should not block
 * (it will either read pending data, or accept a connection).
 */
int libvchan_wait(libvchan_t *ctrl) {
    if (ctrl->socket_fd > 0)
        return wait_for_read(ctrl);
    if (ctrl->server_fd > 0)
        return wait_for_connection(ctrl);
    return -1;
}

// Wait for socket to become readable (or disconnection)
static int wait_for_read(libvchan_t *ctrl) {
    assert(ctrl->socket_fd >= 0);

    // Some data already available?
    if (read_pending(ctrl) > 0)
        return 0;

    // Got disconnected while reading?
    if (ctrl->socket_fd < 0)
        return 0;

    struct pollfd fds[1];
    fds[0].fd = ctrl->socket_fd;
    fds[0].events = POLLIN | POLLHUP;
    while (poll(fds, 1, -1) < 0) {
        if (errno != EINTR) {
            perror("poll wait socket");
            return -1;
        }
    }

    if (fds[0].revents & POLLIN)
        read_pending(ctrl);
    if (ctrl->socket_fd >= 0 && fds[0].revents & POLLHUP)
        close_socket(ctrl);

    return 0;
}

// Wait for a client to connect
static int wait_for_connection(libvchan_t *ctrl) {
    assert(ctrl->server_fd >= 0);
    assert(ctrl->socket_fd < 0);

    int socket_fd = accept(ctrl->server_fd, NULL, NULL);
    if (socket_fd < 0) {
        perror("accept");
        return -1;
    }

    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
        perror("fcntl socket");
        return -1;
    }

    ctrl->socket_fd = socket_fd;
    return 0;
}

// Wait for socket to become writable (or disconnection)
static int wait_for_write(libvchan_t *ctrl) {
    assert(ctrl->socket_fd >= 0);

    struct pollfd fds[1];
    fds[0].fd = ctrl->socket_fd;
    fds[0].events = POLLOUT | POLLHUP;

    while (poll(fds, 1, -1) < 0) {
        if (errno != EINTR) {
            perror("poll wait");
            return -1;
        }
    }

    if (fds[0].revents & POLLHUP)
        close_socket(ctrl);

    return 0;
}

int libvchan_data_ready(libvchan_t *ctrl) {
    if (ctrl->socket_fd >= 0)
        read_pending(ctrl);
    return ring_filled(&ctrl->read_ring);
}

/*
 * How much data we can write without blocking.
 * In the socket case, there's no way to measure that, so we can answer
 * at most 1 byte.
 */
int libvchan_buffer_space(libvchan_t *ctrl) {
    if (ctrl->socket_fd <= 0) {
        return 0;
    }

    struct pollfd fds[1];
    fds[0].fd = ctrl->socket_fd;
    fds[0].events = POLLOUT;

    if (poll(fds, 1, 0) < 0) {
        perror("poll buffer_space");
    }
    return fds[0].revents & POLLOUT ? 1 : 0;
}

int libvchan_is_open(libvchan_t *ctrl) {
    if (ctrl->socket_fd >= 0)
        return VCHAN_CONNECTED;
    if (ctrl->is_new && ctrl->server_fd >= 0)
        return VCHAN_WAITING;
    return VCHAN_DISCONNECTED;
}

/*
 * Read pending data from socket, if any.
 * In case of disconnect, sets socket_fd to -1.
 */
static int read_pending(libvchan_t *ctrl) {
    assert(ctrl->socket_fd >= 0);
    int total = 0;

    for (;;) {
        size_t available = ring_available(&ctrl->read_ring);
        if (available == 0)
            break;
        int ret = read(ctrl->socket_fd, ring_tail(&ctrl->read_ring),
                       available);
        if (ret == 0) {
            close_socket(ctrl);
            break;
        }
        if (ret < 0) {
            if (errno != EAGAIN)
                perror("read pending");
            break;
        }
        total += ret;
        ring_advance_tail(&ctrl->read_ring, ret);
    }
    return total;
}

static void close_socket(libvchan_t *ctrl) {
    if (close(ctrl->socket_fd) < 0)
        perror("close socket");
    ctrl->socket_fd = -1;
    ctrl->is_new = false;
}
