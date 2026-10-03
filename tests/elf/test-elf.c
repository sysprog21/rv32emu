/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Regression tests for ELF loading of untrusted input.
 *
 * An ELF file is attacker controlled input: every offset and size in it is just
 * a number until the loader proves otherwise. These cases each crashed the
 * emulator before the header fields were validated against the file size, so
 * they assert rejection rather than any particular parse result.
 */

#include <assert.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"
#include "io.h"
#include "utils.h"

#define GUEST_MEM_SIZE (4 * 1024 * 1024)

/* Offsets of the fields these tests corrupt, per the ELF32 header layout. */
enum {
    EHDR_SIZE = 52,
    PHDR_SIZE = 32,
    OFF_PHOFF = 28,
    OFF_SHOFF = 32,
    OFF_PHENTSIZE = 42,
    OFF_PHNUM = 44,
    OFF_SHENTSIZE = 46,
    OFF_SHNUM = 48,
    OFF_SHSTRNDX = 50,
};

/* Offset of a field within a program or section header. */
#define PH(field) offsetof(struct Elf32_Phdr, field)
#define SH(field) offsetof(struct Elf32_Shdr, field)

/* Sized to sanitize_path()'s own MAX_PATH_LEN so its bounded strnlen() cannot
 * be flagged as reading past a shorter source object.
 */
static char elf_path[MAX_PATH_LEN] = "/tmp/rv32emu-test-elf-XXXXXX";

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (v >> (8 * i)) & 0xff;
}

/* Build a structurally minimal but well formed RV32 ELF with one PT_LOAD
 * segment, which each test then corrupts in exactly one way.
 */
static size_t build_elf(uint8_t *buf)
{
    memset(buf, 0, EHDR_SIZE + PHDR_SIZE + 4);
    memcpy(buf, "\177ELF", 4);
    buf[4] = 1;               /* ELFCLASS32 */
    buf[5] = 1;               /* little endian */
    buf[6] = 1;               /* EV_CURRENT */
    put16(buf + 16, 2);       /* e_type = ET_EXEC */
    put16(buf + 18, 243);     /* e_machine = EM_RISCV */
    put32(buf + 20, 1);       /* e_version */
    put32(buf + 24, 0x10000); /* e_entry */
    put32(buf + OFF_PHOFF, EHDR_SIZE);
    put32(buf + OFF_SHOFF, 0);
    put16(buf + 40, EHDR_SIZE); /* e_ehsize */
    put16(buf + OFF_PHENTSIZE, PHDR_SIZE);
    put16(buf + OFF_PHNUM, 1);
    put16(buf + OFF_SHENTSIZE, 40);
    put16(buf + OFF_SHNUM, 0);
    put16(buf + OFF_SHSTRNDX, 0);

    uint8_t *ph = buf + EHDR_SIZE;
    put32(ph + 0, 1);                     /* p_type = PT_LOAD */
    put32(ph + 4, EHDR_SIZE + PHDR_SIZE); /* p_offset */
    put32(ph + 8, 0x10000);               /* p_vaddr */
    put32(ph + 12, 0x10000);              /* p_paddr */
    put32(ph + 16, 4);                    /* p_filesz */
    put32(ph + 20, 4);                    /* p_memsz */
    put32(ph + 24, 5);                    /* p_flags = R|X */
    put32(ph + 28, 0x1000);               /* p_align */
    return EHDR_SIZE + PHDR_SIZE + 4;
}

/* Write the image out and report whether the loader accepts and loads it.
 * Rejection at either stage counts as rejection; the point is that neither
 * stage may read outside the file.
 */
static void write_image(const uint8_t *image, size_t size)
{
    FILE *f = fopen(elf_path, "wb");
    assert(f);
    assert(fwrite(image, 1, size, f) == size);
    assert(!fclose(f));
}

static bool loads(const uint8_t *image, size_t size)
{
    write_image(image, size);
    elf_t *e = elf_new();
    assert(e);
    bool ok = elf_open(e, elf_path);
    if (ok) {
        memory_t *mem = memory_new(GUEST_MEM_SIZE);
        assert(mem);
        ok = elf_load(e, mem);
        memory_delete(mem);
    }
    elf_delete(e);
    return ok;
}

static void put_shdr(uint8_t *sh,
                     uint32_t name,
                     uint32_t type,
                     uint32_t offset,
                     uint32_t size)
{
    put32(sh + SH(sh_name), name);
    put32(sh + SH(sh_type), type);
    put32(sh + SH(sh_offset), offset);
    put32(sh + SH(sh_size), size);
}

/* Include names and a symbol so reopening exercises cached file pointers. */
static size_t build_symbol_elf(uint8_t *buf, uint32_t addr, const char *name)
{
    build_elf(buf);
    const uint32_t shoff = EHDR_SIZE + PHDR_SIZE + 4;
    const uint32_t names = shoff + 4 * sizeof(struct Elf32_Shdr);
    static const char section_names[] = "\0.shstrtab\0.strtab\0.symtab";
    const uint32_t strings = names + sizeof(section_names);
    const uint32_t symbols = (strings + 8 + 3) & ~3U;
    const size_t size = symbols + sizeof(struct Elf32_Sym);
    memset(buf + shoff, 0, size - shoff);
    put32(buf + OFF_SHOFF, shoff);
    put16(buf + OFF_SHNUM, 4);
    put16(buf + OFF_SHSTRNDX, 1);

    /* Section 0 stays the all-zero SHN_UNDEF entry. */
    uint8_t *sh = buf + shoff + sizeof(struct Elf32_Shdr);
    put_shdr(sh, 1, SHT_STRTAB, names, sizeof(section_names));
    memcpy(buf + names, section_names, sizeof(section_names));

    sh += sizeof(struct Elf32_Shdr);
    put_shdr(sh, 11, SHT_STRTAB, strings, 8);
    assert(strlen(name) < 7);
    memcpy(buf + strings + 1, name, strlen(name));

    sh += sizeof(struct Elf32_Shdr);
    put_shdr(sh, 19, SHT_SYMTAB, symbols, sizeof(struct Elf32_Sym));
    put32(sh + SH(sh_link), 2); /* .strtab */
    put32(sh + SH(sh_entsize), sizeof(struct Elf32_Sym));
    put32(buf + symbols, 1); /* st_name */
    put32(buf + symbols + 4, addr);
    buf[symbols + 12] = 2; /* STT_FUNC */
    return size;
}

static void test_reopen(void)
{
    uint8_t image[512];
    elf_t *e = elf_new();
    write_image(image, build_symbol_elf(image, 0x10000, "first"));
    assert(elf_open(e, elf_path));
    assert(!strcmp(elf_find_symbol(e, 0x10000), "first"));

    write_image(image, build_symbol_elf(image, 0x10004, "second"));
    assert(elf_open(e, elf_path));
    assert(!elf_find_symbol(e, 0x10000));
    assert(!strcmp(elf_find_symbol(e, 0x10004), "second"));

    /* The loaded image must not depend on the file afterwards: shrinking it
     * would fault every later access to a mapped image with SIGBUS.
     */
    assert(!truncate(elf_path, 0));
    const struct Elf32_Sym *sym = elf_get_symbol(e, "second");
    assert(sym && sym->st_value == 0x10004);
    assert(!strcmp(elf_find_symbol(e, 0x10004), "second"));

    /* A failed reopen releases the previous image and allows another try. */
    assert(!elf_open(e, elf_path));
    assert(!get_elf_header(e));
    assert(!get_elf_first_byte(e));
    write_image(image, build_symbol_elf(image, 0x10008, "third"));
    assert(elf_open(e, elf_path));
    assert(!elf_find_symbol(e, 0x10004));
    assert(!strcmp(elf_find_symbol(e, 0x10008), "third"));
    elf_delete(e);
}

static void test_segment_bounds(void)
{
    uint8_t image[EHDR_SIZE + PHDR_SIZE + 4];
    size_t size = build_elf(image);
    put32(image + EHDR_SIZE + PH(p_vaddr), 12);
    put32(image + EHDR_SIZE + PH(p_memsz), 8);
    write_image(image, size);
    elf_t *e = elf_new();
    assert(elf_open(e, elf_path));

    uint8_t ram[20];
    memset(ram, 0xa5, sizeof(ram));
    memory_t mem = {.mem_base = ram, .mem_size = 16};
    /* Copying fits, but zero filling does not: reject before either write. */
    assert(!elf_load(e, &mem));
    for (size_t i = 0; i < sizeof(ram); i++)
        assert(ram[i] == 0xa5);

    /* The same segment fits exactly when the full memory extent is present. */
    mem.mem_size = sizeof(ram);
    assert(elf_load(e, &mem));
    for (size_t i = 0; i < 12; i++)
        assert(ram[i] == 0xa5);
    for (size_t i = 12; i < sizeof(ram); i++)
        assert(ram[i] == 0);

    /* A later segment out of bounds must not let an earlier one load. */
    uint8_t two[EHDR_SIZE + 2 * PHDR_SIZE + 4];
    build_elf(two);
    uint8_t *ph = two + EHDR_SIZE;
    memcpy(ph + PHDR_SIZE, ph, PHDR_SIZE);
    memset(two + EHDR_SIZE + 2 * PHDR_SIZE, 0, 4);
    put16(two + OFF_PHNUM, 2);
    for (int i = 0; i < 2; i++)
        put32(ph + i * PHDR_SIZE + PH(p_offset), EHDR_SIZE + 2 * PHDR_SIZE);
    put32(ph + PH(p_vaddr), 0); /* fits */
    /* The second segment's memory extent runs past ram. */
    put32(ph + PHDR_SIZE + PH(p_vaddr), 16);
    put32(ph + PHDR_SIZE + PH(p_memsz), 8);
    write_image(two, sizeof(two));
    assert(elf_open(e, elf_path));
    memset(ram, 0xa5, sizeof(ram));
    assert(!elf_load(e, &mem));
    for (size_t i = 0; i < sizeof(ram); i++)
        assert(ram[i] == 0xa5);
    elf_delete(e);
}

int main(void)
{
    int fd = mkstemp(elf_path);
    assert(fd >= 0);
    close(fd);

    uint8_t image[EHDR_SIZE + PHDR_SIZE + 4];
    const size_t size = build_elf(image);

    /* the unmodified image must still load, or the rest proves nothing */
    assert(loads(image, size));

    /* a file too short to hold the header must not be parsed at all */
    assert(!loads(image, EHDR_SIZE - 1));
    assert(!loads(image, 4));

    /* program header table starting past the end of the file */
    build_elf(image);
    put32(image + OFF_PHOFF, 0x40000000);
    assert(!loads(image, size));

    /* program header count that runs the table off the end */
    build_elf(image);
    put16(image + OFF_PHNUM, 0xffff);
    assert(!loads(image, size));

    /* entry size smaller than a program header */
    build_elf(image);
    put16(image + OFF_PHENTSIZE, 8);
    assert(!loads(image, size));

    /* segment contents outside the file: the crash this suite was written for
     */
    build_elf(image);
    put32(image + EHDR_SIZE + 4, 0x40000000); /* p_offset */
    assert(!loads(image, size));

    /* segment length running past the end of the file */
    build_elf(image);
    put32(image + EHDR_SIZE + 16, 0xffffff00); /* p_filesz */
    put32(image + EHDR_SIZE + 20, 0xffffff00); /* p_memsz */
    assert(!loads(image, size));

    /* section header table and name index outside the file */
    build_elf(image);
    put32(image + OFF_SHOFF, 0x40000000);
    put16(image + OFF_SHNUM, 4);
    assert(!loads(image, size));

    build_elf(image);
    put16(image + OFF_SHNUM, 1);
    put16(image + OFF_SHSTRNDX, 9);
    assert(!loads(image, size));

    assert(!loads(image, 0));
    test_reopen();
    test_segment_bounds();

    /* A valid image extended past the ELF32 size limit: a raw_size that
     * wrapped would see only the valid prefix and accept it. Skip when the
     * host cannot create the sparse file; ignore SIGXFSZ so a file size
     * limit fails the call instead of killing the test.
     */
    write_image(image, build_elf(image));
    signal(SIGXFSZ, SIG_IGN);
    if (sizeof(off_t) >= sizeof(uint64_t) &&
        !truncate(elf_path, (off_t) ((UINT64_C(1) << 32) + size))) {
        elf_t *e = elf_new();
        assert(!elf_open(e, elf_path));
        elf_delete(e);
    } else {
        printf("elf: skipping the oversized file check\n");
    }

    unlink(elf_path);
    printf("elf: validation, reopening, and segment bounds passed\n");
    return 0;
}
