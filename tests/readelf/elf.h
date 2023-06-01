#include <stdint.h>

#ifndef ELF_H
#define ELF_H

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t Elf32_Sword;

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_SWord;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

int check_elf_magic_num(const unsigned char *);

#endif  // ELF_H
