/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

/* A minimal ELF parser */

#include <stdint.h>

#include "io.h"
#include "map.h"
#include "riscv.h"

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;

enum {
    EI_MAG0 = 0, /* ELF magic value */
    EI_MAG1 = 1,
    EI_MAG2 = 2,
    EI_MAG3 = 3,
    EI_CLASS = 4,   /* ELF class, one of ELF_IDENT_CLASS_ */
    EI_DATA = 5,    /* Data type of the remainder of the file */
    EI_VERSION = 6, /* Version of the header, ELF_IDENT_VERSION_CURRENT */
    EI_OSABI = 7,
    EI_ABIVERSION = 8,
    EI_PAD = 9, /* nused padding */
    EI_NIDENT = 16,
};

/*
 * Section Types
 */
enum {
    SHT_NULL = 0,     /* Section header table entry unused */
    SHT_PROGBITS = 1, /* Program data */
    SHT_SYMTAB = 2,   /* Symbol table */
    SHT_STRTAB = 3,   /* String table */
    SHT_RELA = 4,     /* Relocation entries with addends */
    SHT_HASH = 5,     /* Symbol hash table */
    SHT_DYNAMIC = 6,  /* Dynamic linking information */
    SHT_NOTE = 7,     /* Notes */
    SHT_NOBITS = 8,   /* Program space with no data (bss) */
    SHT_REL = 9,      /* Relocation entries, no addends */
    SHT_SHLIB = 10,   /* Reserved */
    SHT_DYNSYM = 11,  /* Dynamic linker symbol table */
    SHT_NUM = 12,
    SHT_LOPROC = 0x70000000, /* Start of processor-specific */
    SHT_HIPROC = 0x7fffffff, /* End of processor-specific */
    SHT_LOUSER = 0x80000000, /* Start of application-specific */
    SHT_HIUSER = 0xffffffff, /* End of application-specific */
};

/*
 * Section Attribute Flags
 */
enum {
    SHF_WRITE = 0x1,
    SHF_ALLOC = 0x2,
    SHF_EXECINSTR = 0x4,
    SHF_MERGE = 0x10,
    SHF_STRINGS = 0x20,
    SHF_INFO_LINK = 0x40,
    SHF_LINK_ORDER = 0x80,
    SHF_OS_NONCONFORMING = 0x100,
    SHF_GROUP = 0x200,
    SHF_TLS = 0x400,
    SHF_COMPRESSED = 0x800,
    SHF_MASKOS = 0x0ff00000,
    SHF_MASKPROC = 0xf0000000,
};

/* Elf32 header */
struct Elf32_Ehdr {
    uint8_t e_ident[EI_NIDENT];
    Elf32_Half e_type;      /* Object file type */
    Elf32_Half e_machine;   /* Architecture */
    Elf32_Word e_version;   /* Object file version */
    Elf32_Addr e_entry;     /* Entry point virtual address */
    Elf32_Off e_phoff;      /* Program header table file offset */
    Elf32_Off e_shoff;      /* Section header table file offset */
    Elf32_Word e_flags;     /* Processor-specific flags */
    Elf32_Half e_ehsize;    /* ELF header size in bytes */
    Elf32_Half e_phentsize; /* Program header table entry size */
    Elf32_Half e_phnum;     /* Program header table entry count */
    Elf32_Half e_shentsize; /* Section header table entry size */
    Elf32_Half e_shnum;     /* Section header table entry count */
    Elf32_Half e_shstrndx;  /* Section header string table index */
};

/* Elf32 program header table */
struct Elf32_Phdr {
    Elf32_Word p_type;   /* Type, a combination of ELF_PROGRAM_TYPE_ */
    Elf32_Off p_offset;  /* Offset in the file of the program image */
    Elf32_Addr p_vaddr;  /* Virtual address in memory */
    Elf32_Addr p_paddr;  /* Optional physical address in memory */
    Elf32_Word p_filesz; /* Size of the image in the file */
    Elf32_Word p_memsz;  /* Size of the image in memory */
    Elf32_Word p_flags;  /* Type-specific flags */
    Elf32_Word p_align;  /* Memory alignment in bytes */
};

/* Elf32 section header table */
struct Elf32_Shdr {
    Elf32_Word sh_name;      /* Section name */
    Elf32_Word sh_type;      /* Section type */
    Elf32_Word sh_flags;     /* Section flags */
    Elf32_Addr sh_addr;      /* Section virtual addr at execution */
    Elf32_Off sh_offset;     /* Section file offset */
    Elf32_Word sh_size;      /* Section size in bytes */
    Elf32_Word sh_link;      /* Link to another section */
    Elf32_Word sh_info;      /* Additional section information */
    Elf32_Word sh_addralign; /* Section alignment */
    Elf32_Word sh_entsize;   /* Entry size if section holds table */
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

/* Find symbol from a specified ELF file */
const char *elf_find_symbol(elf_t *e, uint32_t addr);

/* get the range of .data section from the ELF file */
bool elf_get_data_section_range(elf_t *e, uint32_t *start, uint32_t *end);

/* Load the ELF file into a memory abstraction */
bool elf_load(elf_t *e, riscv_t *rv, memory_t *mem);

/* get the ELF header */
struct Elf32_Ehdr *get_elf_header(elf_t *e);

/* get the first byte of ELF raw data */
uint8_t *get_elf_first_byte(elf_t *e);
