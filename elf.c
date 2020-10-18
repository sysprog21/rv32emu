#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "io.h"

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

struct elf_internal {
    const struct Elf32_Ehdr *hdr;
    uint32_t raw_size;
    uint8_t *raw_data;

    /* symbol table map: uint32_t -> (const char *) */
    c_map_t symbols;
};

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

elf_t *elf_new()
{
    elf_t *e = malloc(sizeof(elf_t));
    e->hdr = NULL;
    e->raw_size = 0;
    e->symbols = c_map_init(int, char *, c_map_cmp_uint);
    e->raw_data = malloc(1);
    return e;
}

void elf_delete(elf_t *e)
{
    if (!e)
        return;

    c_map_delete(e->symbols);
    free(e->raw_data);

    free(e);
}

/* release a loaded ELF file */
static void release(elf_t *e)
{
    free(e->raw_data);
    e->raw_data = NULL;
    e->raw_size = 0;
    e->hdr = NULL;
}

/* check if the ELF file header is valid */
static bool is_valid(elf_t *e)
{
    /* check for ELF magic */
    if (e->hdr->e_ident[0] != 0x7f && e->hdr->e_ident[1] != 'E' &&
        e->hdr->e_ident[2] != 'L' && e->hdr->e_ident[3] != 'F') {
        return false;
    }

    /* must be 32bit ELF */
    if (e->hdr->e_ident[EI_CLASS] != ELFCLASS32)
        return false;

    /* check if machine type is RISC-V */
    if (e->hdr->e_machine != EM_RISCV)
        return false;

    return true;
}

/* get section header string table */
static const char *get_sh_string(elf_t *e, int index)
{
    uint32_t offset =
        e->hdr->e_shoff + e->hdr->e_shstrndx * e->hdr->e_shentsize;
    const struct Elf32_Shdr *shdr =
        (const struct Elf32_Shdr *) (e->raw_data + offset);
    return (const char *) (e->raw_data + shdr->sh_offset + index);
}

/* get a section header */
static const struct Elf32_Shdr *get_section_header(elf_t *e, const char *name)
{
    for (int s = 0; s < e->hdr->e_shnum; ++s) {
        uint32_t offset = e->hdr->e_shoff + s * e->hdr->e_shentsize;
        const struct Elf32_Shdr *shdr =
            (const struct Elf32_Shdr *) (e->raw_data + offset);
        const char *sname = get_sh_string(e, shdr->sh_name);
        if (!strcmp(name, sname))
            return shdr;
    }
    return NULL;
}

/* get the ELF string table */
static const char *get_strtab(elf_t *e)
{
    const struct Elf32_Shdr *shdr = get_section_header(e, ".strtab");
    if (!shdr)
        return NULL;

    return (const char *) (e->raw_data + shdr->sh_offset);
}

/* find a symbol entry */
const struct Elf32_Sym *elf_get_symbol(elf_t *e, const char *name)
{
    const char *strtab = get_strtab(e); /* get the string table */
    if (!strtab)
        return NULL;

    /* get the symbol table */
    const struct Elf32_Shdr *shdr = get_section_header(e, ".symtab");
    if (!shdr)
        return NULL;

    /* find symbol table range */
    const struct Elf32_Sym *sym =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
    const struct Elf32_Sym *end =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset +
                                    shdr->sh_size);

    for (; sym < end; ++sym) { /* try to find the symbol */
        const char *sym_name = strtab + sym->st_name;
        if (!strcmp(name, sym_name))
            return sym;
    }

    /* no symbol found */
    return NULL;
}

static void fill_symbols(elf_t *e)
{
    /* initialize the symbol table */
    c_map_clear(e->symbols);
    c_map_insert(e->symbols, &(int){0}, &(char *){NULL});

    /* get the string table */
    const char *strtab = get_strtab(e);
    if (!strtab)
        return;

    /* get the symbol table */
    const struct Elf32_Shdr *shdr = get_section_header(e, ".symtab");
    if (!shdr)
        return;

    /* find symbol table range */
    const struct Elf32_Sym *sym =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
    const struct Elf32_Sym *end =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset +
                                    shdr->sh_size);

    for (; sym < end; ++sym) { /* try to find the symbol */
        const char *sym_name = strtab + sym->st_name;
        switch (ELF_ST_TYPE(sym->st_info)) { /* add to the symbol table */
        case STT_NOTYPE:
        case STT_OBJECT:
        case STT_FUNC:
            c_map_insert(e->symbols, (void *) &(sym->st_value), &sym_name);
        }
    }
}

const char *elf_find_symbol(elf_t *e, uint32_t addr)
{
    if (c_map_empty(e->symbols))
        fill_symbols(e);
    c_map_iter_t it;
    c_map_find(e->symbols, &it, &addr);
    return c_map_at_end(e->symbols, &it) ? NULL : c_map_iter_value(&it, char *);
}

bool elf_load(elf_t *e, struct riscv_t *rv, memory_t *mem)
{
    rv_set_pc(rv, e->hdr->e_entry); /* set the entry point */

    /* loop over all of the program headers */
    for (int p = 0; p < e->hdr->e_phnum; ++p) {
        /* find next program header */
        uint32_t offset = e->hdr->e_phoff + (p * e->hdr->e_phentsize);
        const struct Elf32_Phdr *phdr =
            (const struct Elf32_Phdr *) (e->raw_data + offset);

        /* check this section should be loaded */
        if (phdr->p_type != PT_LOAD)
            continue;

        /* memcpy required range */
        const int to_copy = min(phdr->p_memsz, phdr->p_filesz);
        if (to_copy)
            memory_write(mem, phdr->p_vaddr, e->raw_data + phdr->p_offset,
                         to_copy);

        /* zero fill required range */
        const int to_zero = max(phdr->p_memsz, phdr->p_filesz) - to_copy;
        if (to_zero)
            memory_fill(mem, phdr->p_vaddr + to_copy, to_zero, 0);
    }

    return true;
}

bool elf_open(elf_t *e, const char *path)
{
    /* free previous memory */
    if (e->raw_data)
        release(e);

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    /* get file size */
    fseek(f, 0, SEEK_END);
    e->raw_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (e->raw_size == 0) {
        fclose(f);
        return false;
    }

    /* allocate memory */
    free(e->raw_data);
    e->raw_data = malloc(e->raw_size);

    /* read data into memory */
    const size_t r = fread(e->raw_data, 1, e->raw_size, f);

    fclose(f);
    if (r != e->raw_size) {
        release(e);
        return false;
    }

    /* point to the header */
    e->hdr = (const struct Elf32_Ehdr *) e->raw_data;

    /* check it is a valid ELF file */
    if (!is_valid(e)) {
        release(e);
        return false;
    }

    return true;
}
