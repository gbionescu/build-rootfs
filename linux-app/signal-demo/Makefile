CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
LDLIBS ?=

all: signal_demo

signal_demo: signal_demo.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f signal_demo

.PHONY: all clean
