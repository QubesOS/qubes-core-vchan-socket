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

#ifndef _LIBVCHAN_H
#define _LIBVCHAN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int EVTCHN;

/* return values from libvchan_is_open */
/* remote disconnected or remote domain dead */
#define VCHAN_DISCONNECTED 0
/* connected */
#define VCHAN_CONNECTED 1
/* vchan server initialized, waiting for client to connect */
#define VCHAN_WAITING 2

struct libvchan;
typedef struct libvchan libvchan_t;

libvchan_t *libvchan_server_init(int domain, int port, size_t read_min, size_t write_min);

libvchan_t *libvchan_client_init(int domain, int port);
/* An alternative path for client connection:
 * 1. Call libvchan_client_init_async().
 * 2. Wait for watch_fd to become readable.
 * 3. When readable, call libvchan_client_init_async_finish().
 *
 * Repeat steps 2-3 until libvchan_client_init_async_finish returns 0. Abort on
 * negative values (error).
 * If connection attempt failed or should be aborted, call libvchan_close() to
 * clean up.
 */
libvchan_t *libvchan_client_init_async(int domain, int port, EVTCHN *watch_fd);
int libvchan_client_init_async_finish(libvchan_t *ctrl, bool blocking);


int libvchan_write(libvchan_t *ctrl, const void *data, size_t size);
int libvchan_send(libvchan_t *ctrl, const void *data, size_t size);
int libvchan_read(libvchan_t *ctrl, void *data, size_t size);
int libvchan_recv(libvchan_t *ctrl, void *data, size_t size);
int libvchan_wait(libvchan_t *ctrl);
void libvchan_close(libvchan_t *ctrl);
EVTCHN libvchan_fd_for_select(libvchan_t *ctrl);
int libvchan_is_open(libvchan_t *ctrl);

int libvchan_data_ready(libvchan_t *ctrl);
int libvchan_buffer_space(libvchan_t *ctrl);

#endif /* _LIBVCHAN_H */
