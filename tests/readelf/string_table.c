#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "elf_header.h"
#include "file.h"
#include "section.h"

char *load_string_table(FILE *file,
                        Elf_Internal_Ehdr *ehdr,
                        Elf_Internal_Shdr **shdrs)
{
    Elf_Internal_Shdr *shdr = (Elf_Internal_Shdr *) shdrs + ehdr->e_shstrndx;

    char *buf = read_file(file, shdr->sh_size, shdr->sh_offset);

    return buf;
}

char *get_str_by_index(char *strtab, int index)
{
    return &strtab[index];
}
