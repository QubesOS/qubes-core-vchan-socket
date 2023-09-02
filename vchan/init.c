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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "libvchan.h"
#include "libvchan_private.h"

#define SOCKET_DIR "/var/run/vchan"

static int get_current_domain() {
    const char *s = getenv("VCHAN_DOMAIN");
    return s ? atoi(s) : 0;
}

static libvchan_t *init(
    int server_domain, int client_domain, int port,
    size_t read_min, size_t write_min) {

    libvchan_t *ctrl = malloc(sizeof(*ctrl));
    if (!ctrl)
        return NULL;

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->socket_fd = -1;

    const char *socket_dir = getenv("VCHAN_SOCKET_DIR");
    if (!socket_dir)
        socket_dir = SOCKET_DIR;

    if (asprintf(&ctrl->socket_path, "%s/vchan.%d.%d.%d.sock",
                 socket_dir, server_domain, client_domain, port) < 0) {
        perror("asprintf");
    }

    if (pipe2(ctrl->user_event_pipe, O_NONBLOCK) ||
        pipe2(ctrl->socket_event_pipe, O_NONBLOCK)) {
        perror("pipe");
        libvchan_close(ctrl);
        return NULL;
    }

    if (ring_init(&ctrl->read_ring, read_min) ||
        ring_init(&ctrl->write_ring, write_min)) {
        perror("malloc");
        libvchan_close(ctrl);
        return NULL;
    }

    if (pthread_mutex_init(&ctrl->mutex, NULL)) {
        perror("pthread_mutex_init");
        libvchan_close(ctrl);
        return NULL;
    }

    return ctrl;
}

libvchan_t *libvchan_server_init(int domain, int port, size_t read_min, size_t write_min) {
    libvchan_t *ctrl = init(
        get_current_domain(), domain, port, read_min, write_min);
    if (!ctrl) {
        return NULL;
    }

    ctrl->socket_fd = libvchan__listen(ctrl->socket_path);
    if (ctrl->socket_fd < 0) {
        libvchan_close(ctrl);
        return NULL;
    }

    ctrl->state = VCHAN_WAITING;

    if (pthread_create(&ctrl->thread, NULL, libvchan__server, ctrl)) {
        perror("pthread_create");
        libvchan_close(ctrl);
        return NULL;
    }
    ctrl->thread_started = 1;

    return ctrl;
}

libvchan_t *libvchan_client_init(int domain, int port) {
    libvchan_t *ctrl = init(
        domain, get_current_domain(), port, 1024, 1024);
    if (!ctrl) {
        return NULL;
    }

    ctrl->socket_fd = libvchan__connect(ctrl->socket_path);
    if (ctrl->socket_fd < 0) {
        libvchan_close(ctrl);
        return NULL;
    }

    ctrl->state = VCHAN_CONNECTED;

    if (pthread_create(&ctrl->thread, NULL, libvchan__client, ctrl)) {
        perror("pthread_create");
        libvchan_close(ctrl);
        return NULL;
    }
    ctrl->thread_started = 1;

    return ctrl;
}

libvchan_t *libvchan_client_init_async(int domain, int port, int *watch_fd) {
    int pipe_fds[2];
    libvchan_t *ctrl;

    ctrl = libvchan_client_init(domain, port);
    
    if (!ctrl)
        return NULL;

    if (pipe(pipe_fds) < 0) {
        libvchan_close(ctrl);
        return NULL;
    }

    write(pipe_fds[1], "a", 1);
    close(pipe_fds[1]);
    ctrl->connect_watch_fd = pipe_fds[0];
    *watch_fd = pipe_fds[0];
    return ctrl;
}

int libvchan_client_init_async_finish(libvchan_t *ctrl,
                                      __attribute__((unused)) bool blocking) {
    close(ctrl->connect_watch_fd);
    return 0;
}

void libvchan_close(libvchan_t *ctrl) {
    if (ctrl->thread_started) {
        pthread_mutex_lock(&ctrl->mutex);
        ctrl->shutdown = 1;
        pthread_mutex_unlock(&ctrl->mutex);
        uint8_t byte;
        if (write(ctrl->user_event_pipe[1], &byte, 1) != 1) {
            perror("write close");
            return;
        }
        pthread_join(ctrl->thread, NULL);
    }

    if (ctrl->socket_path)
        free(ctrl->socket_path);

    if (ctrl->socket_fd != -1)
        close(ctrl->socket_fd);

    if (ctrl->user_event_pipe[0]) {
        close(ctrl->user_event_pipe[0]);
        close(ctrl->user_event_pipe[1]);
    }
    if (ctrl->socket_event_pipe[0]) {
        close(ctrl->socket_event_pipe[0]);
        close(ctrl->socket_event_pipe[1]);
    }
    if (ctrl->read_ring.data)
        ring_destroy(&ctrl->read_ring);
    if (ctrl->write_ring.data)
        ring_destroy(&ctrl->write_ring);

    pthread_mutex_destroy(&ctrl->mutex);
    free(ctrl);
}

EVTCHN libvchan_fd_for_select(libvchan_t *ctrl) {
    return ctrl->socket_event_pipe[0];
}
