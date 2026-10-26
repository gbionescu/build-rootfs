CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
LDLIBS ?=

all: pipe_ipc

pipe_ipc: pipe_ipc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f pipe_ipc

.PHONY: all clean
