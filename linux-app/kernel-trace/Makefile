CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g

all: trace_workload

trace_workload: trace_workload.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f trace_workload

.PHONY: all clean
