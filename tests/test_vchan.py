import unittest
import socket
from concurrent.futures import ThreadPoolExecutor
import time

from .vchan import VchanServer, \
    VCHAN_WAITING, VCHAN_DISCONNECTED, VCHAN_CONNECTED


SAMPLE = b'Hello World'


class VchanServerTest(unittest.TestCase):
    def start_server(self):
        server = VchanServer(1, 2, 42)
        server.wait_for_state(VCHAN_WAITING)
        self.addCleanup(server.close)
        return server

    def connect(self, server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(sock.close)
        sock.connect(server.socket_path)
        server.wait_for_state(VCHAN_CONNECTED)
        return sock

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
        server.wait()
        self.assertEqual(server.data_ready(), len(SAMPLE))
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
