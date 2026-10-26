CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
LDLIBS ?=

all: fork_demo

fork_demo: fork_demo.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f fork_demo

.PHONY: all clean
