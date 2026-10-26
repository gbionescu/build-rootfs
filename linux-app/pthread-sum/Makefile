CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -D_GNU_SOURCE
CFLAGS += -pthread
LDLIBS ?=
LDLIBS += -pthread

all: pthread_sum

pthread_sum: pthread_sum.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f pthread_sum

.PHONY: all clean
