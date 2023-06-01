#include "elf.h"
#include "elf_ident.h"

int check_elf_magic_num(const unsigned char *e_ident)
{
    return e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
           e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3;
}
