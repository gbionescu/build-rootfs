#include <stdio.h>
#include <stdlib.h>

#include "elf_symbol.h"

int main(int argc, char **argv)
{
    unsigned long long expected;
    unsigned long long offset = 0;
    char *end = NULL;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <binary> <symbol> <expected-offset>\n",
                argv[0]);
        return 2;
    }

    expected = strtoull(argv[3], &end, 0);
    if (end == argv[3] || *end != '\0') {
        fprintf(stderr, "Invalid expected offset: %s\n", argv[3]);
        return 2;
    }

    if (elf_find_symbol_offset(argv[1], argv[2], &offset) != 0) {
        printf("FAIL: symbol %s not found in %s\n", argv[2], argv[1]);
        return 1;
    }

    if (offset != expected) {
        printf("FAIL: %s offset 0x%llx, expected 0x%llx\n", argv[2], offset,
               expected);
        return 1;
    }

    printf("PASS: %s offset 0x%llx\n", argv[2], offset);
    return 0;
}
