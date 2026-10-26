CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
LDLIBS ?=

all: mmap_file

mmap_file: mmap_file.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f mmap_file

.PHONY: all clean
