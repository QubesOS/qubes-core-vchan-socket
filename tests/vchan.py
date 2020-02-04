# -*- encoding: utf-8 -*-
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2020  Paweł Marczewski  <pawel@invisiblethingslab.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.


from cffi import FFI
import os
import time


VCHAN_DISCONNECTED = 0
VCHAN_CONNECTED = 1
VCHAN_WAITING = 2


class VchanBase:
    def __init__(self):
        self.ffi = FFI()

        self.ffi.cdef("""
struct libvchan;
typedef struct libvchan libvchan_t;

libvchan_t *libvchan_server_init(int domain, int port, size_t read_min, size_t write_min);
libvchan_t *libvchan_client_init(int domain, int port);
int libvchan_write(libvchan_t *ctrl, const void *data, size_t size);
int libvchan_send(libvchan_t *ctrl, const void *data, size_t size);
int libvchan_read(libvchan_t *ctrl, void *data, size_t size);
int libvchan_recv(libvchan_t *ctrl, void *data, size_t size);
int libvchan_wait(libvchan_t *ctrl);
void libvchan_close(libvchan_t *ctrl);
int libvchan_fd_for_select(libvchan_t *ctrl);
int libvchan_is_open(libvchan_t *ctrl);

int libvchan_data_ready(libvchan_t *ctrl);
int libvchan_buffer_space(libvchan_t *ctrl);
""")

        self.lib = self.ffi.dlopen(
            os.path.join(os.path.dirname(__file__), '..', 'vchan',
                         'libvchan-socket.so'))
        self.ctrl = None

    def close(self):
        if self.ctrl is not None:
            self.lib.libvchan_close(self.ctrl)
        self.ctrl = None

    def write(self, data: bytes) -> int:
        result = self.lib.libvchan_write(self.ctrl, data, len(data))
        if result < 0:
            raise VchanException('libvchan_write')
        return result

    def send(self, data: bytes) -> int:
        result = self.lib.libvchan_send(self.ctrl, data, len(data))
        if result < 0:
            raise VchanException('libvchan_send')
        return result

    def read(self, size: int) -> bytes:
        buf = self.ffi.new('char[]', size)
        result = self.lib.libvchan_read(self.ctrl, buf, size)
        if result < 0:
            raise VchanException('libvchan_read')
        return self.ffi.unpack(buf, result)

    def recv(self, size: int) -> bytes:
        buf = self.ffi.new('char[]', size)
        result = self.lib.libvchan_recv(self.ctrl, buf, size)
        if result < 0:
            raise VchanException('libvchan_recv')
        return self.ffi.unpack(buf, result)

    def wait(self):
        result = self.lib.libvchan_wait(self.ctrl)
        if result < 0:
            raise VchanException('libvchan_wait')

    def state(self) -> int:
        return self.lib.libvchan_is_open(self.ctrl)

    def fd_for_select(self) -> int:
        return self.lib.libvchan_fd_for_select(self.ctrl)

    def wait_for(self, pred):
        while not pred():
            self.wait()

    def wait_for_state(self, state: int):
        self.wait_for(lambda: self.state() == state)

    def data_ready(self):
        result = self.lib.libvchan_data_ready(self.ctrl)
        if result < 0:
            raise VchanException('libvchan_data_ready')
        return result

    def buffer_space(self):
        result = self.lib.libvchan_buffer_space(self.ctrl)
        if result < 0:
            raise VchanException('libvchan_buffer_space')
        return result

    def __enter__(self):
        pass

    def __exit__(self, _type, value, traceback):
        self.close()


class VchanException(Exception):
    pass


class VchanServer(VchanBase):
    def __init__(
            self,
            domain=0, remote_domain=0, port=0,
            socket_dir='/tmp',
            read_min=1024,
            write_min=1024,
    ):
        super().__init__()
        os.environ['VCHAN_DOMAIN'] = str(domain)
        os.environ['VCHAN_SOCKET_DIR'] = socket_dir

        self.socket_path = '{}/vchan.{}.{}.{}.sock'.format(
            socket_dir, domain, remote_domain, port)

        self.ctrl = self.lib.libvchan_server_init(
            remote_domain, port, read_min, write_min)
        if self.ctrl == self.ffi.NULL:
            raise VchanException('libvchan_server_init')


class VchanClient(VchanBase):
    def __init__(
            self,
            domain=0, remote_domain=0, port=0,
            socket_dir='/tmp',
    ):
        super().__init__()
        os.environ['VCHAN_DOMAIN'] = str(domain)
        os.environ['VCHAN_SOCKET_DIR'] = socket_dir

        self.socket_path = '{}/vchan.{}.{}.{}.sock'.format(
            socket_dir, remote_domain, domain, port)

        self.ctrl = self.lib.libvchan_client_init(
            remote_domain, port)
        if self.ctrl == self.ffi.NULL:
            raise VchanException('libvchan_client_init')
