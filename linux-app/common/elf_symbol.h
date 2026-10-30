#ifndef LINUX_APP_ELF_SYMBOL_H
#define LINUX_APP_ELF_SYMBOL_H

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8
typedef Elf64_Ehdr elf_ehdr_t;
typedef Elf64_Shdr elf_shdr_t;
typedef Elf64_Phdr elf_phdr_t;
typedef Elf64_Sym elf_sym_t;
#else
typedef Elf32_Ehdr elf_ehdr_t;
typedef Elf32_Shdr elf_shdr_t;
typedef Elf32_Phdr elf_phdr_t;
typedef Elf32_Sym elf_sym_t;
#endif

static inline int
elf_value_to_offset(const unsigned char *base, const elf_ehdr_t *eh,
                    off_t file_size, uint64_t value,
                    unsigned long long *offset)
{
    const elf_phdr_t *phdr;
    unsigned int i;

    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        eh->e_phentsize != sizeof(elf_phdr_t))
        return -1;

    if (eh->e_phoff > (uint64_t)file_size ||
        (uint64_t)eh->e_phnum > ((uint64_t)file_size - eh->e_phoff) /
                                       sizeof(elf_phdr_t))
        return -1;

    phdr = (const elf_phdr_t *)(base + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        uint64_t file_offset;

        if (phdr[i].p_type != PT_LOAD || phdr[i].p_filesz == 0)
            continue;
        if (value < (uint64_t)phdr[i].p_vaddr ||
            value - (uint64_t)phdr[i].p_vaddr >= (uint64_t)phdr[i].p_filesz)
            continue;

        file_offset = value - (uint64_t)phdr[i].p_vaddr +
                      (uint64_t)phdr[i].p_offset;
        if (file_offset >= (uint64_t)file_size)
            return -1;

        *offset = (unsigned long long)file_offset;
        return 0;
    }

    return -1;
}

static inline int
elf_find_symbol_offset(const char *path, const char *name,
                       unsigned long long *offset)
{
    struct stat st;
    const unsigned char *base;
    const elf_ehdr_t *eh;
    const elf_shdr_t *shdr;
    void *map = MAP_FAILED;
    int fd = -1;
    int rc = -1;
    unsigned int i;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(elf_ehdr_t))
        goto out;

    map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
        goto out;

    base = (const unsigned char *)map;
    eh = (const elf_ehdr_t *)base;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0)
        goto out;
    if (eh->e_ident[EI_CLASS] !=
        (__SIZEOF_POINTER__ == 8 ? ELFCLASS64 : ELFCLASS32))
        goto out;
    if (eh->e_shoff == 0 || eh->e_shnum == 0 ||
        eh->e_shentsize != sizeof(elf_shdr_t))
        goto out;
    if (eh->e_shoff > (uint64_t)st.st_size ||
        (uint64_t)eh->e_shnum > ((uint64_t)st.st_size - eh->e_shoff) /
                                      sizeof(elf_shdr_t))
        goto out;

    shdr = (const elf_shdr_t *)(base + eh->e_shoff);
    for (i = 0; i < eh->e_shnum; i++) {
        const elf_shdr_t *strtab;
        const elf_sym_t *syms;
        const char *strings;
        size_t count;
        size_t j;

        if (shdr[i].sh_type != SHT_SYMTAB && shdr[i].sh_type != SHT_DYNSYM)
            continue;
        if (shdr[i].sh_entsize == 0 ||
            shdr[i].sh_entsize > sizeof(elf_sym_t))
            continue;
        if (shdr[i].sh_link >= eh->e_shnum)
            continue;
        if (shdr[i].sh_offset > (uint64_t)st.st_size ||
            shdr[i].sh_size > (uint64_t)st.st_size - shdr[i].sh_offset)
            continue;

        strtab = &shdr[shdr[i].sh_link];
        if (strtab->sh_offset > (uint64_t)st.st_size ||
            strtab->sh_size > (uint64_t)st.st_size - strtab->sh_offset)
            continue;

        syms = (const elf_sym_t *)(base + shdr[i].sh_offset);
        strings = (const char *)(base + strtab->sh_offset);
        count = shdr[i].sh_size / shdr[i].sh_entsize;

        for (j = 0; j < count; j++) {
            const elf_sym_t *sym = (const elf_sym_t *)
                ((const unsigned char *)syms + j * shdr[i].sh_entsize);

            if (sym->st_name >= strtab->sh_size)
                continue;
            if (strcmp(strings + sym->st_name, name) != 0)
                continue;
            if (elf_value_to_offset(base, eh, st.st_size, sym->st_value,
                                    offset) == 0)
                rc = 0;
            goto out;
        }
    }

out:
    if (map != MAP_FAILED)
        munmap(map, (size_t)st.st_size);
    if (fd >= 0)
        close(fd);
    return rc;
}

#endif
