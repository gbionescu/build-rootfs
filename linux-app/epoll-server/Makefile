CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
LDLIBS ?=

all: epoll_server

epoll_server: epoll_server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f epoll_server

.PHONY: all clean
