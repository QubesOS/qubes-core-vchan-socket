PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib64
INCLUDEDIR ?= $(PREFIX)/include

help:
	@echo "make all                   -- build binaries"
	@echo "make install               -- install"
	@echo "make clean                 -- cleanup"

all:
	$(MAKE) -C vchan
	$(MAKE) -C vchan-simple

install:
	install -D -m 0644 vchan/libvchan.h ${DESTDIR}$(INCLUDEDIR)/vchan-socket/libvchan.h
	install -D -m 0644 vchan/vchan-socket.pc ${DESTDIR}$(LIBDIR)/pkgconfig/vchan-socket.pc
	install -D vchan/libvchan-socket.so ${DESTDIR}$(LIBDIR)/libvchan-socket.so

	install -D -m 0644 vchan-simple/libvchan.h ${DESTDIR}$(INCLUDEDIR)/vchan-socket-simple/libvchan.h
	install -D -m 0644 vchan-simple/vchan-socket-simple.pc ${DESTDIR}$(LIBDIR)/pkgconfig/vchan-socket-simple.pc
	install -D vchan-simple/libvchan-socket-simple.so ${DESTDIR}$(LIBDIR)/libvchan-socket-simple.so

clean:
	make -C vchan clean
	make -C vchan-simple clean
