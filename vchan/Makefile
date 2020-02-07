
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
CC ?= gcc
CFLAGS += -g -Wall -Wextra -Werror -fPIC -O2

LIBVCHAN_OBJS = init.o socket.o io.o ring.o
LIBS = -pthread

all: libvchan-socket.so vchan-socket.pc node node-select

$(LIBVCHAN_OBJS): libvchan.h libvchan_private.h

libvchan-socket.so : $(LIBVCHAN_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^

libvchan-socket.a : $(LIBVCHAN_OBJS)
	$(AR) rcs $@ $^

node: node.o libvchan-socket.a
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $^

node-select: node-select.o libvchan-socket.a
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $^

clean:
	rm -f *.o *.so *.a *~ client server node node-select

vchan-socket.pc: vchan-socket.pc.in
	sed -e "s/@VERSION@/`cat ../version`/" \
		-e "s:@PREFIX@:$(PREFIX):" \
		-e "s:@LIBDIR@:$(LIBDIR):" \
		-e "s:@INCLUDEDIR@:$(INCLUDEDIR):" \
		$< > $@
