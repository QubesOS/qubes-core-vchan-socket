import unittest
import socket
from concurrent.futures import ThreadPoolExecutor
import time

from .vchan import VchanServer, VchanClient, \
    VCHAN_WAITING, VCHAN_DISCONNECTED, VCHAN_CONNECTED


SAMPLE = b'Hello World'
BIG_SAMPLE = bytes([
    b'abcdefghijklmnopqrstuvwxyz'[i % 26]
    for i in range(2048)
])

# default buffer size for server and client
BUF_SIZE = 1024


class VchanTestMixin():
    def start_server(self):
        server = VchanServer(1, 2, 42)
        server.wait_for_state(VCHAN_WAITING)
        self.addCleanup(server.close)
        return server

    def connect(self, server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(sock.close)
        sock.connect(server.socket_path)
        return sock

    def start_client(self):
        return VchanClient(2, 1, 42)


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
        self.assertEqual(server.buffer_space(), 1024)
        server.write(SAMPLE)
        self.assertEqual(server.buffer_space(), 1024 - len(SAMPLE))
        sock = self.connect(server)
        self.assertEqual(sock.recv(len(SAMPLE)), SAMPLE)
        self.assertEqual(server.buffer_space(), 1024)

    def test_disconnect_reconnect(self):
        server = self.start_server()
        sock = self.connect(server)
        sock.close()
        server.wait_for_state(VCHAN_DISCONNECTED)
        sock2 = self.connect(server)
        sock2.send(SAMPLE)
        server.write(SAMPLE)
        self.assertEqual(server.read(len(SAMPLE)), SAMPLE)
        self.assertEqual(sock2.recv(len(SAMPLE)), SAMPLE)


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


class VchanClientTest(unittest.TestCase, VchanTestMixin):
    def test_client_connect_and_send(self):
        server = self.start_server()
        client = self.start_client()
        client.write(SAMPLE)
        self.assertEqual(server.read(len(SAMPLE)), SAMPLE)
