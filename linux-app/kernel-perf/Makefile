CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g
TARGET := perf_demo

all: $(TARGET)

$(TARGET): perf_demo.c ../common/elf_symbol.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
