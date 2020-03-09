# libvchan-socket

This is a socket implementation of `libvchan`, analogous to the Xen one
(https://github.com/QubesOS/qubes-core-vchan-xen/).

## Compiling

    make all
    sudo make install

## Usage

Use `<libvchan.h>` in your program. The API should be the same as Xen version.

Compile your program with:

    gcc -c program.c `pkg-config --cflags vchan-socket`
    gcc program.o -o program `pkg-config --libs`

The library will use Unix socket with the following path:

    /var/run/vchan/vchan.<server_domain>.<client_domain>.<port>.sock

This is configurable:

* the local domain number is provided as `VCHAN_DOMAIN` environment variable
  (because it cannot be passed using the API),
* the default directory can be provided as `VCHAN_SOCKET_DIR`, which is useful
  if you don't want to run as root.

The server will accept connections at that path, and the client will try to
connect (and reconnect). Only one connection at a time is supported.

## Architecture

To (mostly) keep libvchan's semantics, `libvchan-socket` starts a separate
thread responsible for connection management and socket I/O. Data is exchanged
using a pair of ring buffers.

## `libvchan-socket-simple`

`libvchan-socket-simple` is a simpler implementation that does not use a
separate thread. It should be less error-prone, and make debugging (e.g. with
`strace`) easier, but it has limitations that mean programs will need to be
adapted:

* `libvchan_buffer_space()` doesn't tell you how much data you can write, it just
  returns 1 if you can write anything at all. That means you have to do the
  buffering yourself.

* Sending will always block if you are disconnected.

* `libvchan_wait` currently waits only for reads to unblock, now
  writes. Similarly, read events on `libvchan_fd_for_select()` will not tell
  you anything about writes.

## Tests

See `tests/` and `run-tests` script. The tests are written in Python and use
`cffi` for interfacing with C code.
