#pragma once

/* A minimal ELF parser */

#include <stdint.h>

#include "c_map.h"
#include "io.h"
#include "riscv.h"

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;

enum {
    EI_MAG0 = 0,
    EI_MAG1 = 1,
    EI_MAG2 = 2,
    EI_MAG3 = 3,
    EI_CLASS = 4,
    EI_DATA = 5,
    EI_VERSION = 6,
    EI_OSABI = 7,
    EI_ABIVERSION = 8,
    EI_PAD = 9,
    EI_NIDENT = 16,
};

struct Elf32_Sym {
    Elf32_Word st_name;
    Elf32_Addr st_value;
    Elf32_Word st_size;
    uint8_t st_info;
    uint8_t st_other;
    Elf32_Half st_shndx;
};

typedef struct elf_internal elf_t;

elf_t *elf_new();
void elf_delete(elf_t *e);

/* Open an ELF file from specified path */
bool elf_open(elf_t *e, const char *path);

/* Find a symbol entry */
const struct Elf32_Sym *elf_get_symbol(elf_t *e, const char *name);

/* Find symbole from a specified ELF file */
const char *elf_find_symbol(elf_t *e, uint32_t addr);

/* get the range of .data section from the ELF file */
bool elf_get_data_section_range(elf_t *e, uint32_t *start, uint32_t *end);

/* Load the ELF file into a memory abstraction */
bool elf_load(elf_t *e, struct riscv_t *rv, memory_t *mem);
