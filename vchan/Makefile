
CC ?= gcc
CFLAGS += -g -Wall -Wextra -Werror -fPIC -O2

LIBVCHAN_OBJS = init.o thread.o io.o
LIBS = -pthread

all: libvchan-socket.so node node-select

$(LIBVCHAN_OBJS): libvchan.h libvchan_private.h

libvchan-socket.so : $(LIBVCHAN_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^

libvchan-socket.a : $(LIBVCHAN_OBJS)
	$(AR) rcs $@ $^

node: node.o libvchan-socket.a
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $^

node-select: node-select.o libvchan-socket.a
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $^
