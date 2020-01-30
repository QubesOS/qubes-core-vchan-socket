
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

#include "libvchan.h"
#include "libvchan_private.h"

#define CONNECT_DELAY_MS 100

static void server_loop(libvchan_t *ctrl, int server_fd);
static int comm_loop(libvchan_t *ctrl, int socket_fd);

void *libvchan__server(void *arg) {
    libvchan_t *ctrl = arg;
    fprintf(stderr, "Starting server at %s\n", ctrl->socket_path);

    if (unlink(ctrl->socket_path) && errno != ENOENT ) {
        perror("unlink");
        return NULL;
    }

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return NULL;
    }

    if (fcntl(server_fd, F_SETFL, O_NONBLOCK)) {
        perror("fcntl server_fd");
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctrl->socket_path, sizeof(addr.sun_path) - 1);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("bind");
        return NULL;
    }
    if (listen(server_fd, 1)) {
        perror("listen");
        return NULL;
    }

    server_loop(ctrl, server_fd);

    return NULL;
}

void *libvchan__client(void *arg) {
    libvchan_t *ctrl = arg;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctrl->socket_path, sizeof(addr.sun_path) - 1);

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = CONNECT_DELAY_MS * 1000;

    int done = 0;
    while (!done) {
        int connected = 0;
        fprintf(stderr, "Connecting to %s\n", ctrl->socket_path);

        int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            perror("socket");
            return NULL;
        }

        while (!connected) {
            if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr))) {
                if (errno != ECONNREFUSED && errno != ENOENT) {
                    perror("connect");
                    return NULL;
                }
                nanosleep(&ts, NULL);
            } else {
                connected = 1;
            }
        }
        fprintf(stderr, "Connected\n");

        if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
            perror("fcntl socket");
            return NULL;
        }

        done = comm_loop(ctrl, socket_fd);
        if (close(socket_fd)) {
            perror("close socket");
            return NULL;
        }
    }

    return NULL;
}

static void server_loop(libvchan_t *ctrl, int server_fd) {
    int done = 0;
    while (!done) {
        fprintf(stderr, "Waiting for connection\n");
        int connected = 0;

        struct pollfd fds[1];
        fds[0].fd = server_fd;
        fds[0].events = POLLIN;
        while (!connected) {
            if (poll(fds, 1, CONNECT_DELAY_MS) < 0) {
                perror("poll server_fd");
                return;
            }
            pthread_mutex_lock(&ctrl->mutex);
            done = ctrl->shutdown;
            pthread_mutex_unlock(&ctrl->mutex);
            if (done)
                return;
            connected = fds[0].revents & POLLIN;
        }

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

        done = comm_loop(ctrl, socket_fd);
        if (close(socket_fd)) {
            perror("close socket");
            return;
        }
    }
}

static int comm_loop(libvchan_t *ctrl, int socket_fd) {
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
            return 1;
        }

        pthread_mutex_lock(&ctrl->mutex);

        if (fds[1].revents & POLLIN) {
            libvchan__drain_pipe(ctrl->user_event_pipe[0]);
        }

        if (ctrl->shutdown) {
            pthread_mutex_unlock(&ctrl->mutex);
            return 1;
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
                            return 1;
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
                            return 1;
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
                return 1;
            }
        }

        pthread_mutex_unlock(&ctrl->mutex);
    }
    return 0;
}
