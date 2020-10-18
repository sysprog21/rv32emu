#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "c_map.h"

namespace ELF
{
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

enum {
    EM_RISCV = 243,
};

enum {
    ELFCLASS32 = 1,
    ELFCLASS64 = 2,
};

enum {
    ET_NONE = 0,
    ET_REL = 1,
    ET_EXEC = 2,
    ET_DYN = 3,
    ET_CORE = 4,
};

enum {
    PF_X = 1,
    PF_W = 2,
    PF_R = 4,
};

enum {
    SHT_NULL = 0,
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_STRTAB = 3,
    SHT_RELA = 4,
    SHT_HASH = 5,
    SHT_DYNAMIC = 6,
    SHT_NOTE = 7,
    SHT_NOBITS = 8,
    SHT_REL = 9,
    SHT_SHLIB = 10,
    SHT_DYNSYM = 11,
    SHT_INIT_ARRAY = 14,
    SHT_FINI_ARRAY = 15,
    SHT_PREINIT_ARRAY = 16,
    SHT_GROUP = 17,
    SHT_SYMTAB_SHNDX = 18,
};

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

enum {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
    PT_TLS = 7,
};

enum {
    STT_NOTYPE = 0,
    STT_OBJECT = 1,
    STT_FUNC = 2,
    STT_SECTION = 3,
    STT_FILE = 4,
    STT_COMMON = 5,
    STT_TLS = 6,
};

#define ELF_ST_BIND(x) ((x) >> 4)
#define ELF_ST_TYPE(x) (((unsigned int) x) & 0xf)

struct Elf32_Ehdr {
    uint8_t e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

struct Elf32_Shdr {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
};

struct Elf32_Sym {
    Elf32_Word st_name;
    Elf32_Addr st_value;
    Elf32_Word st_size;
    uint8_t st_info;
    uint8_t st_other;
    Elf32_Half st_shndx;
};

}  // namespace ELF

struct memory_t;

// a minimal ELF parser
struct elf_t {
    elf_t();
    ~elf_t() { free(raw_data); }

    // Open an ELF file from disk
    bool open(const char *path);

    // release a loaded ELF file
    void release()
    {
        free(raw_data);
        raw_data = NULL;
        raw_size = 0;
        hdr = nullptr;
    }

    // check the ELF file header is valid
    bool is_valid() const
    {
        // check for ELF magic
        if (hdr->e_ident[0] != 0x7f && hdr->e_ident[1] != 'E' &&
            hdr->e_ident[2] != 'L' && hdr->e_ident[3] != 'F') {
            return false;
        }

        // must be 32bit ELF
        if (hdr->e_ident[ELF::EI_CLASS] != ELF::ELFCLASS32)
            return false;

        // check machine type is RISCV
        if (hdr->e_machine != ELF::EM_RISCV)
            return false;
        return true;
    }

    // get section header string table
    const char *get_sh_string(int index) const
    {
        uint32_t offset = hdr->e_shoff + hdr->e_shstrndx * hdr->e_shentsize;
        const ELF::Elf32_Shdr *shdr =
            (const ELF::Elf32_Shdr *) (data() + offset);
        return (const char *) (data() + shdr->sh_offset + index);
    }

    // get a section header
    const ELF::Elf32_Shdr *get_section_header(const char *name) const
    {
        for (int s = 0; s < hdr->e_shnum; ++s) {
            uint32_t offset = hdr->e_shoff + s * hdr->e_shentsize;
            const ELF::Elf32_Shdr *shdr =
                (const ELF::Elf32_Shdr *) (data() + offset);
            const char *sname = get_sh_string(shdr->sh_name);
            if (!strcmp(name, sname))
                return shdr;
        }
        return nullptr;
    }

    // get the load range of a section
    bool get_data_section_range(uint32_t &start, uint32_t &end) const
    {
        const ELF::Elf32_Shdr *shdr = get_section_header(".data");
        if (!shdr)
            return false;
        if (shdr->sh_type == ELF::SHT_NOBITS)
            return false;
        start = shdr->sh_addr;
        end = start + shdr->sh_size;
        return true;
    }

    // get the ELF string table
    const char *get_strtab() const
    {
        const ELF::Elf32_Shdr *shdr = get_section_header(".strtab");
        if (!shdr)
            return nullptr;
        return (const char *) (data() + shdr->sh_offset);
    }

    // find a symbol entry
    const ELF::Elf32_Sym *get_symbol(const char *name) const
    {
        // get the string table
        const char *strtab = get_strtab();
        if (!strtab)
            return nullptr;

        // get the symbol table
        const ELF::Elf32_Shdr *shdr = get_section_header(".symtab");
        if (!shdr)
            return nullptr;

        // find symbol table range
        const ELF::Elf32_Sym *sym =
            (const ELF::Elf32_Sym *) (data() + shdr->sh_offset);
        const ELF::Elf32_Sym *end =
            (const ELF::Elf32_Sym *) (data() + shdr->sh_offset + shdr->sh_size);

        // try to find the symbol
        for (; sym < end; ++sym) {
            const char *sym_name = strtab + sym->st_name;
            if (!strcmp(name, sym_name))
                return sym;
        }
        // cant find the symbol
        return nullptr;
    }

    // load the ELF file into a memory abstraction
    bool load(struct riscv_t *rv, memory_t &mem) const;

    const uint8_t *data() const { return raw_data; }

    uint32_t size() const { return raw_size; }

    const char *find_symbol(uint32_t addr)
    {
        if (c_map_empty(symbols))
            fill_symbols();
        c_map_iterator_t it;
        c_map_find(symbols, &it, &addr);
        return c_map_at_end(symbols, &it) ? nullptr
                                          : c_map_iterator_value(&it, char *);
    }

protected:
    void fill_symbols();

    const ELF::Elf32_Ehdr *hdr;
    uint32_t raw_size;
    uint8_t *raw_data;

    // symbol table map: uint32_t -> const char *
    c_map_t symbols;
};
