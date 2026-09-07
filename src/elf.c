/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "io.h"
#include "utils.h"

#if HAVE_MMAP
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
/* fallback to standard I/O text stream */
#include <stdio.h>
#endif

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

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

elf_t *elf_new(void)
{
    elf_t *e = malloc(sizeof(elf_t));
    assert(e);
    e->hdr = NULL;
    e->raw_size = 0;
    e->symbols = map_init(int, char *, map_cmp_uint);
    e->raw_data = NULL;
    return e;
}

void elf_delete(elf_t *e)
{
    if (!e)
        return;

    map_delete(e->symbols);
#if HAVE_MMAP
    if (e->raw_data)
        munmap(e->raw_data, e->raw_size);
#else
    free(e->raw_data);
#endif
    free(e);
}

/* release a loaded ELF file */
static void release(elf_t *e)
{
#if !HAVE_MMAP
    free(e->raw_data);
#endif

    e->raw_data = NULL;
    e->raw_size = 0;
    e->hdr = NULL;
}

/* check if the ELF file header is valid */
/* Does [offset, offset + size) fall inside the mapped file? Written so that
 * neither operand can wrap: the sum is never formed.
 */
static inline bool in_file(const elf_t *e, uint32_t offset, uint32_t size)
{
    return offset <= e->raw_size && size <= e->raw_size - offset;
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

    /* section header table, and the entries it claims to hold. The offset and
     * stride must both be 4-aligned: the entries are cast to structs holding
     * uint32_t, so a misaligned table is undefined behavior, not merely slow.
     * Every real toolchain emits aligned tables.
     */
    if (e->hdr->e_shnum) {
        if (e->hdr->e_shentsize < sizeof(struct Elf32_Shdr) ||
            (e->hdr->e_shoff & 3) || (e->hdr->e_shentsize & 3) ||
            e->hdr->e_shnum > e->raw_size / e->hdr->e_shentsize ||
            !in_file(e, e->hdr->e_shoff,
                     (uint32_t) e->hdr->e_shnum * e->hdr->e_shentsize))
            return false;

        /* the section name string table is indexed by e_shstrndx */
        if (e->hdr->e_shstrndx >= e->hdr->e_shnum)
            return false;

        for (int i = 0; i < e->hdr->e_shnum; ++i) {
            const struct Elf32_Shdr *shdr =
                (const struct Elf32_Shdr *) (e->raw_data + e->hdr->e_shoff +
                                             (uint32_t) i *
                                                 e->hdr->e_shentsize);
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

    /* program header table, same alignment reasoning as above */
    if (e->hdr->e_phnum) {
        if (e->hdr->e_phentsize < sizeof(struct Elf32_Phdr) ||
            (e->hdr->e_phoff & 3) || (e->hdr->e_phentsize & 3) ||
            e->hdr->e_phnum > e->raw_size / e->hdr->e_phentsize ||
            !in_file(e, e->hdr->e_phoff,
                     (uint32_t) e->hdr->e_phnum * e->hdr->e_phentsize))
            return false;

        for (int i = 0; i < e->hdr->e_phnum; ++i) {
            const struct Elf32_Phdr *phdr =
                (const struct Elf32_Phdr *) (e->raw_data + e->hdr->e_phoff +
                                             (uint32_t) i *
                                                 e->hdr->e_phentsize);
            /* p_filesz must not exceed p_memsz. elf_load() derives its
             * zero-fill length from max(p_memsz, p_filesz), so an inverted
             * pair makes it clear bytes past the end of the segment.
             */
            if (phdr->p_type == PT_LOAD &&
                (phdr->p_filesz > phdr->p_memsz ||
                 !in_file(e, phdr->p_offset, phdr->p_filesz)))
                return false;
        }
    }

    return true;
}

/* get the nth section header; the table was validated by is_valid() */
static const struct Elf32_Shdr *get_shdr(const elf_t *e, int n)
{
    return (const struct Elf32_Shdr *) (e->raw_data + e->hdr->e_shoff +
                                        (uint32_t) n * e->hdr->e_shentsize);
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

/* find a symbol entry */
const struct Elf32_Sym *elf_get_symbol(elf_t *e, const char *name)
{
    const struct Elf32_Shdr *strtab = get_strtab(e); /* the string table */
    if (!strtab)
        return NULL;

    /* get the symbol table */
    const struct Elf32_Shdr *shdr = get_symtab(e);
    if (!shdr)
        return NULL;

    /* find symbol table range */
    const struct Elf32_Sym *syms =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
    const uint32_t n = symbol_count(shdr);

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
    /* initialize the symbol table */
    map_clear(e->symbols);
    map_insert(e->symbols, &(int) {0}, &(char *) {NULL});

    /* get the string table */
    const struct Elf32_Shdr *strtab = get_strtab(e);
    if (!strtab)
        return;

    /* get the symbol table */
    const struct Elf32_Shdr *shdr = get_symtab(e);
    if (!shdr)
        return;

    /* find symbol table range */
    const struct Elf32_Sym *syms =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
    const uint32_t n = symbol_count(shdr);

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
    return map_at_end(e->symbols, &it) ? NULL : map_iter_value(&it, char *);
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
    /* loop over all of the program headers */
    for (int p = 0; p < e->hdr->e_phnum; ++p) {
        /* find next program header */
        const struct Elf32_Phdr *phdr =
            (const struct Elf32_Phdr *) (e->raw_data + e->hdr->e_phoff +
                                         (uint32_t) p * e->hdr->e_phentsize);

        /* check this section should be loaded */
        if (phdr->p_type != PT_LOAD)
            continue;

        /* memcpy required range */
        const int to_copy = min(phdr->p_memsz, phdr->p_filesz);
        if (to_copy && !memory_write(mem, phdr->p_vaddr,
                                     e->raw_data + phdr->p_offset, to_copy))
            return false;

        /* zero fill required range */
        const int to_zero = max(phdr->p_memsz, phdr->p_filesz) - to_copy;
        if (to_zero && !memory_fill(mem, phdr->p_vaddr + to_copy, to_zero, 0))
            return false;
    }

    return true;
}

bool elf_open(elf_t *e, const char *input)
{
    /* free previous memory */
    if (e->raw_data)
        release(e);

    char *path = sanitize_path(input);
    if (!path)
        return false;

#if HAVE_MMAP
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        goto free_path;

    /* get file size */
    struct stat st;
    fstat(fd, &st);
    e->raw_size = st.st_size;

    /* map or unmap files or devices into memory.
     * The beginning of the file is ELF header.
     */
    e->raw_data = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (e->raw_data == MAP_FAILED)
        goto free_fd;
    close(fd);

#else  /* fallback to standard I/O text stream */
    FILE *f = fopen(path, "rb");
    if (!f)
        goto free_path;

    /* get file size */
    fseek(f, 0, SEEK_END);
    e->raw_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (!e->raw_size)
        goto free_fd;

    /* allocate memory */
    free(e->raw_data);
    e->raw_data = malloc(e->raw_size);
    assert(e->raw_data);

    /* read data into memory */
    const size_t r = fread(e->raw_data, 1, e->raw_size, f);
    fclose(f);
    if (r != e->raw_size)
        goto free_path;
#endif /* HAVE_MMAP */

    /* point to the header */
    e->hdr = (const struct Elf32_Ehdr *) e->raw_data;

    /* check it is a valid ELF file */
    if (!is_valid(e))
        goto free_path;

    free(path);
    return true;

free_fd:
#if HAVE_MMAP
    close(fd);
#else
    fclose(f);
#endif

free_path:
    free(path);

    release(e);
    return false;
}

struct Elf32_Ehdr *get_elf_header(elf_t *e)
{
    return (struct Elf32_Ehdr *) e->hdr;
}

uint8_t *get_elf_first_byte(elf_t *e)
{
    return (uint8_t *) e->raw_data;
}
