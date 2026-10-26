CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
CFLAGS += -pthread
LDLIBS ?=
LDLIBS += -pthread -lrt

all: shm_sem

shm_sem: shm_sem.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f shm_sem

.PHONY: all clean
