CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g

TARGET := ebpf_demo

all: $(TARGET)

$(TARGET): ebpf_demo.c bpf_insn.h bpf_sys.h ../common/elf_symbol.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
