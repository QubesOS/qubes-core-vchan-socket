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

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

#include "libvchan.h"
#include "libvchan_private.h"

#define CONNECT_DELAY_MS 100

int libvchan__listen(const char *socket_path) {
    int server_fd;

    if (unlink(socket_path) && errno != ENOENT) {
        perror("unlink");
        return -1;
    }

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("bind");
        close(server_fd);
        return -1;
    }
    if (listen(server_fd, 1)) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int libvchan__connect(const char *socket_path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = CONNECT_DELAY_MS * 1000;

    int socket_fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        perror("socket");
        return -1;
    }

    while (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr))) {
        if (errno != ECONNREFUSED && errno != ENOENT) {
            perror("connect");
            close(socket_fd);
            return -1;
        }
        nanosleep(&ts, NULL);
    }

    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
        perror("fcntl socket");
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}
