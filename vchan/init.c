/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
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

    if (asprintf(&ctrl->socket_path, "%s/vchan.%d.%d.%d.sock",
                 SOCKET_DIR, server_domain, client_domain, port) < 0) {
        perror("asprintf");
    }

    if (pipe2(ctrl->user_event_pipe, O_NONBLOCK) ||
        pipe2(ctrl->socket_event_pipe, O_NONBLOCK)) {
        perror("pipe");
        libvchan_close(ctrl);
        return NULL;
    }

    ctrl->read_ring.size = read_min + 1;
    ctrl->read_ring.data = malloc(ctrl->read_ring.size);

    ctrl->write_ring.size = write_min + 1;
    ctrl->write_ring.data = malloc(ctrl->write_ring.size);

    if (!ctrl->read_ring.data || !ctrl->write_ring.data) {
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

    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, libvchan__server, ctrl)) {
        perror("pthread_create");
        libvchan_close(ctrl);
        return NULL;
    }

    return ctrl;
}

void libvchan_close(libvchan_t *ctrl) {
    if (ctrl->socket_path)
        free(ctrl->socket_path);

    if (ctrl->user_event_pipe[0]) {
        close(ctrl->user_event_pipe[0]);
        close(ctrl->user_event_pipe[1]);
    }
    if (ctrl->socket_event_pipe[0]) {
        close(ctrl->socket_event_pipe[0]);
        close(ctrl->socket_event_pipe[1]);
    }
    if (ctrl->read_ring.data)
        free(ctrl->read_ring.data);
    if (ctrl->read_ring.data)
        free(ctrl->write_ring.data);

    pthread_mutex_destroy(&ctrl->mutex);
    free(ctrl);
}

#pragma GCC diagnostic ignored "-Wunused-parameter"

libvchan_t *libvchan_client_init(int domain, int port) {
    libvchan_t *ctrl = init(
        get_current_domain(), domain, port, 10, 10);
    if (!ctrl) {
        return NULL;
    }

    fprintf(stderr, "Connecting to %s\n", ctrl->socket_path);

    return ctrl;
}
