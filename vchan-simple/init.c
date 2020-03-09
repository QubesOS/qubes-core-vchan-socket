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
    size_t read_min) {

    libvchan_t *ctrl = malloc(sizeof(*ctrl));
    if (!ctrl)
        return NULL;

    ctrl->socket_path = NULL;
    ctrl->server_fd = -1;
    ctrl->socket_fd = -1;
    ctrl->is_new = true;

    const char *socket_dir = getenv("VCHAN_SOCKET_DIR");
    if (!socket_dir)
        socket_dir = SOCKET_DIR;

    if (asprintf(&ctrl->socket_path, "%s/vchan.%d.%d.%d.sock",
                 socket_dir, server_domain, client_domain, port) < 0) {
        perror("asprintf");
        free(ctrl);
        return NULL;
    }

    if (ring_init(&ctrl->read_ring, read_min) < 0) {
        free(ctrl);
        return NULL;
    }

    return ctrl;
}

libvchan_t *libvchan_server_init(int domain, int port,
                                 size_t read_min,
                                 __attribute__((unused)) size_t write_min) {
    libvchan_t *ctrl = init(
        get_current_domain(), domain, port, read_min);
    if (!ctrl) {
        return NULL;
    }

    ctrl->server_fd = libvchan__listen(ctrl->socket_path);
    if (ctrl->server_fd < 0) {
        libvchan_close(ctrl);
        return NULL;
    }

    return ctrl;
}

libvchan_t *libvchan_client_init(int domain, int port) {
    libvchan_t *ctrl = init(
        domain, get_current_domain(), port, 1024);
    if (!ctrl) {
        return NULL;
    }

    ctrl->socket_fd = libvchan__connect(ctrl->socket_path);
    if (ctrl->socket_fd < 0) {
        libvchan_close(ctrl);
        return NULL;
    }

    return ctrl;
}

void libvchan_close(libvchan_t *ctrl) {
    if (ctrl->server_fd >= 0)
        if (close(ctrl->server_fd))
            perror("close server_fd");
    if (ctrl->socket_fd >= 0)
        if (close(ctrl->socket_fd))
            perror("close socket_fd");
    free(ctrl);
}

EVTCHN libvchan_fd_for_select(libvchan_t *ctrl) {
    if (ctrl->socket_fd >= 0)
        return ctrl->socket_fd;
    return ctrl->server_fd;
}
