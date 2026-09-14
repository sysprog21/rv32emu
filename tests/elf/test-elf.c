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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"
#include "io.h"

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

/* Sized to sanitize_path()'s own MAX_PATH_LEN so its bounded strnlen() cannot
 * be flagged as reading past a shorter source object.
 */
static char elf_path[1024] = "/tmp/rv32emu-test-elf-XXXXXX";

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
static bool loads(const uint8_t *image, size_t size)
{
    FILE *f = fopen(elf_path, "wb");
    assert(f);
    assert(fwrite(image, 1, size, f) == size);
    fclose(f);

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

    unlink(elf_path);
    printf("elf: all malformed images rejected\n");
    return 0;
}
