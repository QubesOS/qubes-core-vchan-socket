from cffi import FFI
import os


class VchanBase:
    def __init__(self):
        self.ffi = FFI()

        self.ffi.cdef("""
struct libvchan;
typedef struct libvchan libvchan_t;

libvchan_t *libvchan_server_init(int domain, int port, size_t read_min, size_t write_min);
int libvchan_write(libvchan_t *ctrl, const void *data, size_t size);
int libvchan_read(libvchan_t *ctrl, void *data, size_t size);
int libvchan_wait(libvchan_t *ctrl);
void libvchan_close(libvchan_t *ctrl);

int libvchan_data_ready(libvchan_t *ctrl);
int libvchan_buffer_space(libvchan_t *ctrl);
""")

        self.lib = self.ffi.dlopen(
            os.path.join(os.path.dirname(__file__), '..', 'vchan',
                         'libvchan-socket.so'))


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

    def close(self):
        if self.ctrl is not None:
            self.lib.libvchan_close(self.ctrl)
        self.ctrl = None

    def write(self, data: bytes) -> int:
        result = self.lib.libvchan_write(self.ctrl, data, len(data))
        if result < 0:
            raise VchanException('libvchan_write')
        return result

    def read(self, size: int) -> bytes:
        buf = self.ffi.new('char[]', size)
        result = self.lib.libvchan_read(self.ctrl, buf, size)
        if result < 0:
            raise VchanException('libvchan_read')
        return self.ffi.unpack(buf, result)

    def wait(self):
        result = self.lib.libvchan_wait(self.ctrl)
        if result < 0:
            raise VchanException('libvchan_wait')

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
