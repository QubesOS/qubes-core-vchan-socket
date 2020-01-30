
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#include "libvchan.h"
#include "libvchan_private.h"

static void server_loop(libvchan_t *ctrl, int server_fd);
static void comm_loop(libvchan_t *ctrl, int socket_fd);

void *libvchan__server(void *arg) {
    libvchan_t *ctrl = arg;
    fprintf(stderr, "Starting server at %s\n", ctrl->socket_path);

    if (unlink(ctrl->socket_path) && errno != ENOENT ) {
        perror("unlink");
        return NULL;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctrl->socket_path, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("bind");
        return NULL;
    }
    if (listen(fd, 1)) {
        perror("listen");
        return NULL;
    }

    server_loop(ctrl, fd);

    return NULL;
}

static void server_loop(libvchan_t *ctrl, int server_fd) {
    for (;;) {
        fprintf(stderr, "Waiting for connection\n");
        int socket_fd = accept(server_fd, NULL, NULL);
        if (socket_fd < 0) {
            perror("accept");
            return;
        }

        fprintf(stderr, "Got connection\n");
        if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
            perror("fcntl socket");
            return;
        }

        comm_loop(ctrl, socket_fd);
        if (close(socket_fd)) {
            perror("close socket");
            return;
        }
    }
}

static void comm_loop(libvchan_t *ctrl, int socket_fd) {
    struct pollfd fds[2];
    fds[0].fd = socket_fd;
    fds[1].fd = ctrl->user_event_pipe[0];
    fds[1].events = POLLIN;
    int eof = 0;
    while (!eof) {
        pthread_mutex_lock(&ctrl->mutex);
        fds[0].events = 0;
        if (ring_available(&ctrl->read_ring) > 0)
            fds[0].events |= POLLIN;
        if (ring_filled(&ctrl->write_ring) > 0)
            fds[0].events |= POLLOUT;

        pthread_mutex_unlock(&ctrl->mutex);

        if (poll(fds, 2, -1) < 0) {
            perror("poll");
            return;
        }

        pthread_mutex_lock(&ctrl->mutex);

        if (fds[1].revents & POLLIN) {
            libvchan__drain_pipe(ctrl->user_event_pipe[0]);
        }

        int notify = 0;

        // Read from socket into read_ring
        if (fds[0].revents & POLLIN) {
            for (int i = 0; i < 2; i++) {
                int cap = ring_available_contig(&ctrl->read_ring);
                if (cap > 0) {
                    int count = read(
                        socket_fd, &ctrl->read_ring.data[ctrl->read_ring.end], cap);
                    if (count == 0) {
                        eof = 1;
                    } else if (count < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            count = 0;
                        else {
                            perror("read from socket");
                            pthread_mutex_unlock(&ctrl->mutex);
                            return;
                        }
                    } else
                        notify = 1;
                    ring_advance_end(&ctrl->read_ring, count);
                }
            }
        }

        if (fds[0].revents & POLLOUT) {
            // Write from write_ring into socket
            for (int i = 0; i < 2; i++) {
                int fill = ring_filled_contig(&ctrl->write_ring);
                if (fill > 0) {
                    int count = write(
                        socket_fd,
                        &ctrl->write_ring.data[ctrl->write_ring.start],
                        fill);
                    if (count < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            count = 0;
                        else {
                            perror("write to socket");
                            pthread_mutex_unlock(&ctrl->mutex);
                            return;
                        }
                    } else if (count > 0)
                        notify = 1;
                    fprintf(stderr, "write %d\n", count);
                    ring_advance_start(&ctrl->write_ring, count);
                }
            }
        }

        if (notify) {
            uint8_t byte = 0;
            if (write(ctrl->socket_event_pipe[1], &byte, 1) != 1) {
                perror("write");
                pthread_mutex_unlock(&ctrl->mutex);
                return;
            }
        }

        pthread_mutex_unlock(&ctrl->mutex);
    }
}
