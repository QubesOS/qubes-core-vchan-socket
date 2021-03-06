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


import unittest
import socket
from concurrent.futures import ThreadPoolExecutor
import time

from .vchan import VchanServer, VchanClient, \
    VCHAN_WAITING, VCHAN_DISCONNECTED, VCHAN_CONNECTED

# default buffer size for server and client
BUF_SIZE = 4096

SAMPLE = b'Hello World'
BIG_SAMPLE = bytes([
    b'abcdefghijklmnopqrstuvwxyz'[i % 26]
    for i in range(BUF_SIZE * 2)
])


class VchanTestMixin():
    lib = 'vchan/libvchan-socket.so'

    def start_server(self):
        server = VchanServer(self.lib, 1, 2, 42)
        server.wait_for_state(VCHAN_WAITING)
        self.addCleanup(server.close)
        return server

    def connect(self, server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(sock.close)
        sock.connect(server.socket_path)
        return sock

    def start_client(self):
        return VchanClient(self.lib, 2, 1, 42)


class VchanServerTest(unittest.TestCase, VchanTestMixin):
    def test_connect_and_read(self):
        server = self.start_server()
        sock = self.connect(server)
        sock.send(SAMPLE)
        data = server.read(15)
        self.assertEqual(data, SAMPLE)

    def test_read_then_connect(self):
        server = self.start_server()

        with ThreadPoolExecutor() as executor:
            future = executor.submit(server.read, len(SAMPLE))
            time.sleep(0.1)
            sock = self.connect(server)
            sock.send(SAMPLE)
            self.assertEqual(future.result(), SAMPLE)

    def test_connect_and_write(self):
        server = self.start_server()
        sock = self.connect(server)
        self.assertEqual(server.write(SAMPLE), len(SAMPLE))
        self.assertEqual(sock.recv(len(SAMPLE)), SAMPLE)

    def test_write_then_connect(self):
        server = self.start_server()
        self.assertEqual(server.write(SAMPLE), len(SAMPLE))
        sock = self.connect(server)
        self.assertEqual(sock.recv(len(SAMPLE)), SAMPLE)

    def test_data_ready(self):
        server = self.start_server()
        sock = self.connect(server)
        self.assertEqual(server.data_ready(), 0)
        sock.send(SAMPLE)
        server.wait_for(lambda: server.data_ready() == len(SAMPLE))
        self.assertEqual(server.read(len(SAMPLE)), SAMPLE)
        self.assertEqual(server.data_ready(), 0)

    def test_buffer_space(self):
        server = self.start_server()
        self.assertEqual(server.buffer_space(), BUF_SIZE)
        server.write(SAMPLE)
        self.assertEqual(server.buffer_space(), BUF_SIZE - len(SAMPLE))
        sock = self.connect(server)
        self.assertEqual(sock.recv(len(SAMPLE)), SAMPLE)
        self.assertEqual(server.buffer_space(), BUF_SIZE)


class SimpleVchanServerTest(VchanServerTest):
    lib = 'vchan-simple/libvchan-socket-simple.so'

    def test_buffer_space(self):
        server = self.start_server()
        self.assertEqual(server.buffer_space(), 0)
        sock = self.connect(server)
        server.wait_for_state(VCHAN_CONNECTED)
        self.assertEqual(server.buffer_space(), 1)
        sock.close()
        server.wait_for_state(VCHAN_DISCONNECTED)
        self.assertEqual(server.buffer_space(), 0)

    @unittest.skip('not possible in simple implementation')
    def test_write_then_connect(self):
        pass


class VchanBufferTest(unittest.TestCase, VchanTestMixin):
    def test_read_less(self):
        server = self.start_server()
        sock = self.connect(server)
        sock.send(SAMPLE)
        self.assertEqual(server.read(len(SAMPLE) * 2), SAMPLE)

    def test_recv_all(self):
        server = self.start_server()
        sock = self.connect(server)
        sock.send(SAMPLE)
        with ThreadPoolExecutor() as executor:
            future = executor.submit(server.recv, len(SAMPLE) * 2)
            time.sleep(0.1)
            sock.send(SAMPLE)
            self.assertEqual(future.result(), SAMPLE * 2)

    def test_send_big(self):
        server = self.start_server()
        sock = self.connect(server)
        sock.send(BIG_SAMPLE[:BUF_SIZE+10])
        self.assertEqual(server.read(BUF_SIZE+10), BIG_SAMPLE[:BUF_SIZE])
        self.assertEqual(server.read(10), BIG_SAMPLE[BUF_SIZE:BUF_SIZE+10])

    def test_wrap_around(self):
        server = self.start_server()
        sock = self.connect(server)
        sock.send(BIG_SAMPLE[:BUF_SIZE // 3])
        self.assertEqual(server.read(BUF_SIZE // 3),
                         BIG_SAMPLE[:BUF_SIZE // 3])
        sock.send(BIG_SAMPLE[:BUF_SIZE])
        self.assertEqual(server.read(BUF_SIZE),
                         BIG_SAMPLE[:BUF_SIZE])


class SimpleVchanBufferTest(VchanBufferTest):
    lib = 'vchan-simple/libvchan-socket-simple.so'


class VchanClientTest(unittest.TestCase, VchanTestMixin):
    def test_client_connect_and_send(self):
        server = self.start_server()
        client = self.start_client()
        client.write(SAMPLE)
        self.assertEqual(server.read(len(SAMPLE)), SAMPLE)


class SimpleVchanClientTest(VchanClientTest):
    lib = 'vchan-simple/libvchan-socket-simple.so'
