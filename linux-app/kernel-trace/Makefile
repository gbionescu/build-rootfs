CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g

TARGET := trace_workload

all: $(TARGET)

$(TARGET): trace_workload.c ../common/elf_symbol.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
