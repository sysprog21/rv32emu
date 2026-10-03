/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "io.h"
#include "utils.h"

enum {
    EM_RISCV = 243,
};

enum {
    ELFCLASS32 = 1,
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

#define ELF_ST_TYPE(x) (((unsigned int) x) & 0xf)

struct elf_internal {
    const struct Elf32_Ehdr *hdr;
    uint32_t raw_size;
    uint8_t *raw_data;

    /* symbol table map: uint32_t -> (const char *) */
    map_t symbols;
};

elf_t *elf_new(void)
{
    elf_t *e = malloc(sizeof(elf_t));
    assert(e);
    e->hdr = NULL;
    e->raw_size = 0;
    e->symbols = map_init(uint32_t, char *, map_cmp_uint);
    e->raw_data = NULL;
    return e;
}

/* Release file storage and every cached pointer into it. */
static void release(elf_t *e)
{
    map_clear(e->symbols);
    free(e->raw_data);
    e->raw_data = NULL;
    e->raw_size = 0;
    e->hdr = NULL;
}

void elf_delete(elf_t *e)
{
    if (!e)
        return;

    release(e);
    map_delete(e->symbols);
    free(e);
}

/* Table accessors: callers must first validate the table bounds. */
static const struct Elf32_Shdr *get_shdr(const elf_t *e, int n)
{
    return (const struct Elf32_Shdr *) (e->raw_data + e->hdr->e_shoff +
                                        (uint32_t) n * e->hdr->e_shentsize);
}

static const struct Elf32_Phdr *get_phdr(const elf_t *e, int n)
{
    return (const struct Elf32_Phdr *) (e->raw_data + e->hdr->e_phoff +
                                        (uint32_t) n * e->hdr->e_phentsize);
}

/* Does [offset, offset + size) fall inside the loaded file? Written so that
 * neither operand can wrap: the sum is never formed.
 */
static inline bool in_file(const elf_t *e, uint32_t offset, uint32_t size)
{
    return offset <= e->raw_size && size <= e->raw_size - offset;
}

/* A header table of @num entries, @entsize bytes apart, lies inside the file.
 * The offset and stride must both be 4-aligned: the entries are cast to structs
 * holding uint32_t, so a misaligned table is undefined behavior, not merely
 * slow. Every real toolchain emits aligned tables. Two 16-bit factors cannot
 * overflow the 32-bit product.
 */
static inline bool table_in_file(const elf_t *e,
                                 uint32_t offset,
                                 uint16_t num,
                                 uint16_t entsize,
                                 size_t min_entsize)
{
    return entsize >= min_entsize && !(offset & 3) && !(entsize & 3) &&
           in_file(e, offset, (uint32_t) num * entsize);
}

/* Section contents, bounds-checked. */
static inline bool section_in_file(const elf_t *e,
                                   const struct Elf32_Shdr *shdr)
{
    /* These section types have no file payload. */
    return shdr->sh_type == SHT_NULL || shdr->sh_type == SHT_NOBITS ||
           in_file(e, shdr->sh_offset, shdr->sh_size);
}

/* Fetch a NUL-terminated string out of a string-table section. is_valid()
 * has already proven the section lies in the file and ends in a NUL, so any
 * index inside it yields a string that terminates without running off the
 * end.
 */
static const char *str_in_section(const elf_t *e,
                                  const struct Elf32_Shdr *strtab,
                                  uint32_t index)
{
    if (!strtab || strtab->sh_type != SHT_STRTAB || index >= strtab->sh_size)
        return NULL;
    return (const char *) (e->raw_data + strtab->sh_offset + index);
}

/* Validate the whole header structure up front, so that every traversal
 * downstream can index the tables without re-checking. An ELF is attacker
 * controlled input: nothing below this function may trust a field that was
 * not proven in bounds here.
 */
static bool is_valid(elf_t *e)
{
    /* the header itself must be present before any field is read */
    if (e->raw_size < sizeof(struct Elf32_Ehdr))
        return false;

    /* check for ELF magic */
    if (memcmp(e->hdr->e_ident, "\177ELF", 4))
        return false;

    /* must be 32bit ELF */
    if (e->hdr->e_ident[EI_CLASS] != ELFCLASS32)
        return false;

    /* check if machine type is RISC-V */
    if (e->hdr->e_machine != EM_RISCV)
        return false;

    /* section header table, and the entries it claims to hold */
    if (e->hdr->e_shnum) {
        if (!table_in_file(e, e->hdr->e_shoff, e->hdr->e_shnum,
                           e->hdr->e_shentsize, sizeof(struct Elf32_Shdr)))
            return false;

        /* the section name string table is indexed by e_shstrndx */
        if (e->hdr->e_shstrndx >= e->hdr->e_shnum)
            return false;

        for (int i = 0; i < e->hdr->e_shnum; ++i) {
            const struct Elf32_Shdr *shdr = get_shdr(e, i);
            if (!section_in_file(e, shdr))
                return false;

            /* symbol table contents are cast to structs as well */
            if (shdr->sh_type == SHT_SYMTAB && (shdr->sh_offset & 3))
                return false;

            /* A string table is indexed by name offsets taken from other
             * headers. Requiring a trailing NUL is what makes every such
             * lookup terminate inside the section.
             */
            if (shdr->sh_type == SHT_STRTAB &&
                (!shdr->sh_size ||
                 e->raw_data[shdr->sh_offset + shdr->sh_size - 1]))
                return false;
        }
    }

    /* program header table */
    if (e->hdr->e_phnum) {
        if (!table_in_file(e, e->hdr->e_phoff, e->hdr->e_phnum,
                           e->hdr->e_phentsize, sizeof(struct Elf32_Phdr)))
            return false;

        for (int i = 0; i < e->hdr->e_phnum; ++i) {
            const struct Elf32_Phdr *phdr = get_phdr(e, i);
            /* File bytes must fit inside the segment's memory extent:
             * elf_load() computes the zero-fill length as the unsigned
             * p_memsz - p_filesz, which an inverted pair would wrap.
             */
            if (phdr->p_type == PT_LOAD &&
                (phdr->p_filesz > phdr->p_memsz ||
                 !in_file(e, phdr->p_offset, phdr->p_filesz)))
                return false;
        }
    }

    return true;
}

/* get section header string table */
static const char *get_sh_string(elf_t *e, uint32_t index)
{
    return str_in_section(e, get_shdr(e, e->hdr->e_shstrndx), index);
}

/* get a section header */
static const struct Elf32_Shdr *get_section_header(elf_t *e, const char *name)
{
    for (int s = 0; s < e->hdr->e_shnum; ++s) {
        const struct Elf32_Shdr *shdr = get_shdr(e, s);
        if (shdr->sh_type == SHT_NULL)
            continue;
        const char *sname = get_sh_string(e, shdr->sh_name);
        if (sname && !strcmp(name, sname))
            return shdr;
    }
    return NULL;
}

/* get the ELF string table section, or NULL when absent */
static const struct Elf32_Shdr *get_strtab(elf_t *e)
{
    const struct Elf32_Shdr *shdr = get_section_header(e, ".strtab");
    return shdr && shdr->sh_type == SHT_STRTAB ? shdr : NULL;
}

static const struct Elf32_Shdr *get_symtab(elf_t *e)
{
    const struct Elf32_Shdr *shdr = get_section_header(e, ".symtab");
    return shdr && shdr->sh_type == SHT_SYMTAB ? shdr : NULL;
}

/* Number of whole symbol entries in a symbol table section. sh_size is
 * attacker controlled and need not be a multiple of the entry size, so the
 * partial tail entry is dropped rather than read across the section end.
 */
static inline uint32_t symbol_count(const struct Elf32_Shdr *shdr)
{
    return shdr->sh_size / sizeof(struct Elf32_Sym);
}

/* Locate the symbol entries and the string table naming them. Returns NULL,
 * with *n and *strtab untouched, when either table is absent.
 */
static const struct Elf32_Sym *get_symbols(elf_t *e,
                                           const struct Elf32_Shdr **strtab,
                                           uint32_t *n)
{
    const struct Elf32_Shdr *names = get_strtab(e);
    const struct Elf32_Shdr *shdr = names ? get_symtab(e) : NULL;
    if (!shdr)
        return NULL;

    *strtab = names;
    *n = symbol_count(shdr);
    return (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
}

/* find a symbol entry */
const struct Elf32_Sym *elf_get_symbol(elf_t *e, const char *name)
{
    const struct Elf32_Shdr *strtab;
    uint32_t n;
    const struct Elf32_Sym *syms = get_symbols(e, &strtab, &n);
    if (!syms)
        return NULL;

    for (uint32_t i = 0; i < n; ++i) { /* try to find the symbol */
        const char *sym_name = str_in_section(e, strtab, syms[i].st_name);
        if (sym_name && !strcmp(name, sym_name))
            return &syms[i];
    }

    /* no symbol found */
    return NULL;
}

static void fill_symbols(elf_t *e)
{
    /* initialize the symbol table; release() emptied it */
    map_insert(e->symbols, &(uint32_t) {0}, &(char *) {NULL});

    const struct Elf32_Shdr *strtab;
    uint32_t n;
    const struct Elf32_Sym *syms = get_symbols(e, &strtab, &n);
    if (!syms)
        return;

    for (uint32_t i = 0; i < n; ++i) { /* try to find the symbol */
        const char *sym_name = str_in_section(e, strtab, syms[i].st_name);
        if (!sym_name)
            continue;
        switch (ELF_ST_TYPE(syms[i].st_info)) { /* add to the symbol table */
        case STT_NOTYPE:
        case STT_OBJECT:
        case STT_FUNC:
            map_insert(e->symbols, (void *) &(syms[i].st_value), &sym_name);
        }
    }
}

const char *elf_find_symbol(elf_t *e, uint32_t addr)
{
    if (map_empty(e->symbols))
        fill_symbols(e);
    map_iter_t it;
    map_find(e->symbols, &it, &addr);
    return map_at_end(&it) ? NULL : map_iter_value(&it, char *);
}

bool elf_get_data_section_range(elf_t *e, uint32_t *start, uint32_t *end)
{
    const struct Elf32_Shdr *shdr = get_section_header(e, ".data");
    if (!shdr || shdr->sh_type == SHT_NOBITS)
        return false;

    *start = shdr->sh_addr;
    *end = *start + shdr->sh_size;
    return true;
}

/* A quick ELF briefer:
 *    +--------------------------------+
 *    | ELF Header                     |--+
 *    +--------------------------------+  |
 *    | Program Header                 |  |
 *    +--------------------------------+  |
 * +->| Sections: .text, .strtab, etc. |  |
 * |  +--------------------------------+  |
 * +--| Section Headers                |<-+
 *    +--------------------------------+
 *
 * Finding the section header table (SHT):
 *   File start + ELF_header.shoff -> section_header table
 * Finding the string table for section header names:
 *   section_header table[ELF_header.shstrndx] -> section header for name table
 * Finding data for section headers:
 *   File start + section_header.offset -> section Data
 */
bool elf_load(elf_t *e, memory_t *mem)
{
    /* Validate every segment before modifying guest memory, so a rejected
     * image leaves it untouched. memory_new() caps mem_size at 4 GiB, so a
     * contained extent cannot wrap the RV32 address space.
     */
    for (int p = 0; p < e->hdr->e_phnum; ++p) {
        const struct Elf32_Phdr *phdr = get_phdr(e, p);
        if (phdr->p_type == PT_LOAD && phdr->p_memsz &&
            !GUEST_RAM_CONTAINS(mem, phdr->p_vaddr, phdr->p_memsz))
            return false;
    }

    for (int p = 0; p < e->hdr->e_phnum; ++p) {
        const struct Elf32_Phdr *phdr = get_phdr(e, p);

        /* check this section should be loaded */
        if (phdr->p_type != PT_LOAD || !phdr->p_memsz)
            continue;

        /* The pass above proved both ranges fit, so neither call can fail. */
        memory_write(mem, phdr->p_vaddr, e->raw_data + phdr->p_offset,
                     phdr->p_filesz);
        memory_fill(mem, phdr->p_vaddr + phdr->p_filesz,
                    phdr->p_memsz - phdr->p_filesz, 0);
    }

    return true;
}

/* Decode the executable sections one instruction at a time, looking for a
 * 32-bit instruction that match() accepts. Sections are walked rather than
 * segments because read-only data shares the executable segment, and whole
 * instructions rather than every halfword because the middle of one instruction
 * often looks like another. An ELF without section headers is assumed to
 * contain one.
 */
bool elf_has_insn(elf_t *e, bool (*match)(uint32_t insn))
{
    if (!e->hdr->e_shnum)
        return true;

    for (int s = 0; s < e->hdr->e_shnum; ++s) {
        const struct Elf32_Shdr *shdr = get_shdr(e, s);
        if (shdr->sh_type != SHT_PROGBITS || !(shdr->sh_flags & SHF_EXECINSTR))
            continue;

        const uint8_t *text = e->raw_data + shdr->sh_offset;
        for (uint32_t i = 0, len; i + 2 <= shdr->sh_size; i += len) {
            len = (text[i] & 3) == 3 ? 4 : 2; /* 2 for compressed */
            if (len == 4 && i + 4 <= shdr->sh_size &&
                match(text[i] | text[i + 1] << 8 | text[i + 2] << 16 |
                      (uint32_t) text[i + 3] << 24))
                return true;
        }
    }
    return false;
}

bool elf_open(elf_t *e, const char *input)
{
    /* free previous memory */
    release(e);

    char *path = sanitize_path(input);
    if (!path)
        return false;

    /* Read the whole file rather than map it: validation must see the bytes
     * every later access sees, and a mapped file truncated afterwards would
     * raise SIGBUS on the next access.
     */
    FILE *f = fopen(path, "rb");
    free(path);
    if (!f)
        return false;

    /* get file size, then read data into memory */
    long file_size = 0;
    bool ok = !fseek(f, 0, SEEK_END) && (file_size = ftell(f)) > 0 &&
              (uint64_t) file_size <= UINT32_MAX && !fseek(f, 0, SEEK_SET);
    if (ok) {
        e->raw_size = (uint32_t) file_size;
        e->raw_data = malloc(e->raw_size);
        ok =
            e->raw_data && fread(e->raw_data, 1, e->raw_size, f) == e->raw_size;
    }
    fclose(f);

    /* point to the header, and check it is a valid ELF file */
    e->hdr = (const struct Elf32_Ehdr *) e->raw_data;
    if (!ok || !is_valid(e)) {
        release(e);
        return false;
    }
    return true;
}

struct Elf32_Ehdr *get_elf_header(elf_t *e)
{
    return (struct Elf32_Ehdr *) e->hdr;
}

uint8_t *get_elf_first_byte(elf_t *e)
{
    return (uint8_t *) e->raw_data;
}
