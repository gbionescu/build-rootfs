CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g

all: perf_counter

perf_counter: perf_counter.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f perf_counter

.PHONY: all clean
