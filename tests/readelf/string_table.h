#include "elf_header.h"
#include "section.h"

#ifndef STRING_TABLE_H
#define STRING_TABLE_H

char *load_string_table(FILE *, Elf_Internal_Ehdr *, Elf_Internal_Shdr **);

char *get_str_by_index(char *, int);

#endif  // STRING_TABLE_H
