#include <stdio.h>
#include <stdlib.h>
#include <memory>

#include "elf.h"
#include "io.h"

#include "riscv.h"

elf_t::elf_t() : hdr(nullptr), raw_size(0) {}

bool elf_t::load(struct riscv_t *rv, memory_t &mem) const
{
    // set the entry point
    rv_set_pc(rv, hdr->e_entry);
    // loop over all of the program headers
    for (int p = 0; p < hdr->e_phnum; ++p) {
        // find next program header
        uint32_t offset = hdr->e_phoff + (p * hdr->e_phentsize);
        const ELF::Elf32_Phdr *phdr =
            (const ELF::Elf32_Phdr *) (data() + offset);

        // check this section should be loaded
        if (phdr->p_type != ELF::PT_LOAD)
            continue;

        // memcpy required range
        const int to_copy = std::min(phdr->p_memsz, phdr->p_filesz);
        if (to_copy)
            mem.write(phdr->p_vaddr, data() + phdr->p_offset, to_copy);

        // zero fill required range
        const int to_zero = std::max(phdr->p_memsz, phdr->p_filesz) - to_copy;
        if (to_zero)
            mem.fill(phdr->p_vaddr + to_copy, to_zero, 0);
    }

    return true;
}

bool elf_t::open(const char *path)
{
    // free previous memory
    if (raw_data)
        release();

    // open the file handle
    FILE *fd = fopen(path, "rb");
    if (!fd)
        return false;

    // get file size
    fseek(fd, 0, SEEK_END);
    raw_size = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    if (raw_size == 0) {
        fclose(fd);
        return false;
    }

    // allocate memory
    raw_data.reset(new uint8_t[raw_size]);

    // read data into memory
    const size_t read = fread(raw_data.get(), 1, raw_size, fd);

    // close file handle
    fclose(fd);
    if (read != raw_size) {
        release();
        return false;
    }

    // point to the header
    hdr = (const ELF::Elf32_Ehdr *) data();

    // check it is a valid ELF file
    if (!is_valid()) {
        release();
        return false;
    }

    return true;
}

void elf_t::fill_symbols()
{
    // init the symbol table
    symbols.clear();
    symbols[0] = "NULL";

    // get the string table
    const char *strtab = get_strtab();
    if (!strtab)
        return;

    // get the symbol table
    const ELF::Elf32_Shdr *shdr = get_section_header(".symtab");
    if (!shdr)
        return;

    // find symbol table range
    const ELF::Elf32_Sym *sym =
        (const ELF::Elf32_Sym *) (data() + shdr->sh_offset);
    const ELF::Elf32_Sym *end =
        (const ELF::Elf32_Sym *) (data() + shdr->sh_offset + shdr->sh_size);

    // try to find the symbol
    for (; sym < end; ++sym) {
        const char *sym_name = strtab + sym->st_name;
        // add to the symbol table
        switch (ELF_ST_TYPE(sym->st_info)) {
        case ELF::STT_NOTYPE:
        case ELF::STT_OBJECT:
        case ELF::STT_FUNC:
            symbols[uint32_t(sym->st_value)] = sym_name;
        }
    }
}
