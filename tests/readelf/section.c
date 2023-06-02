#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "elf_header.h"
#include "file.h"
#include "section.h"
#include "string_table.h"

Elf_Internal_Shdr **get_section_headers(FILE *file,
                                        Elf_Internal_Ehdr *elf_header,
                                        int shnum,
                                        int is64)
{
    off_t offset = elf_header->e_shoff;
    size_t size = (is64 ? sizeof(Elf64_Shdr) : sizeof(Elf32_Shdr)) * shnum;
    char *buf = read_file(file, size, offset);

    Elf_Internal_Shdr **headers =
        (Elf_Internal_Shdr **) malloc(sizeof(Elf_Internal_Shdr) * shnum);
    if (!headers) {
        perror("readelf");
        exit(errno);
    }

    for (int i = 0; i < shnum; i++) {
        Elf_Internal_Shdr *header = (Elf_Internal_Shdr *) headers + i;

        if (is64) {
            Elf64_Shdr *shdr = (Elf64_Shdr *) (buf + sizeof(Elf64_Shdr) * i);

            header->sh_name = shdr->sh_name;
            header->sh_type = shdr->sh_type;
            header->sh_flags = shdr->sh_flags;
            header->sh_addr = shdr->sh_addr;
            header->sh_offset = shdr->sh_offset;
            header->sh_size = shdr->sh_size;
            header->sh_link = shdr->sh_link;
            header->sh_info = shdr->sh_info;
            header->sh_addralign = shdr->sh_addralign;
            header->sh_entsize = shdr->sh_entsize;
        } else {
            Elf32_Shdr *shdr = (Elf32_Shdr *) (buf + sizeof(Elf32_Shdr) * i);

            header->sh_name = shdr->sh_name;
            header->sh_type = shdr->sh_type;
            header->sh_flags = shdr->sh_flags;
            header->sh_addr = shdr->sh_addr;
            header->sh_offset = shdr->sh_offset;
            header->sh_size = shdr->sh_size;
            header->sh_link = shdr->sh_link;
            header->sh_info = shdr->sh_info;
            header->sh_addralign = shdr->sh_addralign;
            header->sh_entsize = shdr->sh_entsize;
        }
    }

    free(buf);

    return headers;
}

char *get_section_type(uint32_t sh_type)
{
    static char buf[32];

    switch (sh_type) {
    case SHT_NULL:
        return "NULL";
    case SHT_PROGBITS:
        return "PROGBITS";
    case SHT_SYMTAB:
        return "SYMTAB";
    case SHT_STRTAB:
        return "STRTAB";
    case SHT_RELA:
        return "RELA";
    case SHT_HASH:
        return "HASH";
    case SHT_DYNAMIC:
        return "DYNAMIC";
    case SHT_NOTE:
        return "NOTE";
    case SHT_NOBITS:
        return "NOBITS";
    case SHT_REL:
        return "REL";
    case SHT_SHLIB:
        return "SHLIB";
    case SHT_DYNSYM:
        return "DYNSYM";
    case SHT_INIT_ARRAY:
        return "INIT_ARRAY";
    case SHT_FINI_ARRAY:
        return "FINI_ARRAY";
    case SHT_PREINIT_ARRAY:
        return "PREINIT_ARRAY";
    case SHT_GROUP:
        return "GROUP";
    case SHT_SYMTAB_SHNDX:
        return "STMTAB SECTION INDICES";
    default:
        snprintf(buf, sizeof(buf), "%08x: <unknown>", sh_type);
    }

    return buf;
}

char *get_section_flags(const uint64_t sh_flags)
{
    static char buf[64];
    char *p = buf;
    uint64_t mask = 1;

    while (sh_flags >= mask) {
        switch (sh_flags & mask) {
        case SHF_WRITE:
            *p = 'W';
            break;
        case SHF_ALLOC:
            *p = 'A';
            break;
        case SHF_EXECINSTR:
            *p = 'X';
            break;
        case SHF_MERGE:
            *p = 'M';
            break;
        case SHF_STRINGS:
            *p = 'S';
            break;
        case SHF_INFO_LINK:
            *p = 'I';
            break;
        case SHF_LINK_ORDER:
            *p = 'L';
            break;
        case SHF_OS_NONCONFORMING:
            *p = 'O';
            break;
        case SHF_GROUP:
            *p = 'G';
            break;
        case SHF_TLS:
            *p = 'T';
            break;
        case SHF_COMPRESSED:
            *p = 'C';
            break;
        default:
            mask <<= 1;
            continue;
        }

        p++;
        mask <<= 1;
    }

    *p = '\0';

    return buf;
}

void display_section_header(FILE *file, Elf_Internal_Ehdr *elf_header)
{
    if (check_elf_magic_num(elf_header->e_ident)) {
        fprintf(stderr,
                "readelf: Error: Not an ELF file - it has wrong magic bytes at "
                "the starts\n");
        exit(1);
    }

    fprintf(stdout, "There are %d section headers, starting at offset 0x%x\n\n",
            elf_header->e_shnum, elf_header->e_shoff);
    fprintf(stdout, "Section Headers:\n");
    // Header
    fprintf(stdout,
            "  [Nr] Name              Type            Addr     Off    Size   "
            "ES Flg Lk Inf Al\n");

    Elf_Internal_Shdr **shdrs =
        get_section_headers(file, elf_header, elf_header->e_shnum,
                            elf_header->e_ident[EI_CLASS] == ELFCLASS64);

    char *strtab = load_string_table(file, elf_header, shdrs);

    for (int i = 0; i < elf_header->e_shnum; i++) {
        Elf_Internal_Shdr *shdr = (Elf_Internal_Shdr *) shdrs + i;

        // section No.
        fprintf(stdout, "  [%2d]", i);
        // TODO: section name.
        fprintf(stdout, " %-17s", get_str_by_index(strtab, shdr->sh_name));
        // section type
        fprintf(stdout, " %-15s", get_section_type(shdr->sh_type));
        // section address
        fprintf(stdout, " %08lx", shdr->sh_addr);
        // section offset
        fprintf(stdout, " %06lx", shdr->sh_offset);
        // section size
        fprintf(stdout, " %06lx", shdr->sh_size);
        // section entry size
        fprintf(stdout, " %02x", shdr->sh_entsize);
        // section attribute flags
        fprintf(stdout, " %3s", get_section_flags(shdr->sh_flags));
        // table index link
        fprintf(stdout, " %2u", shdr->sh_link);
        // section extra infomation
        fprintf(stdout, " %3u", shdr->sh_info);
        // section address alignment constraints
        fprintf(stdout, " %2lu", shdr->sh_addralign);

        fprintf(stdout, "\n");
    }

    fprintf(stdout, "Key to Flags:\n");
    fprintf(stdout,
            "  W (write), A (alloc), X (execute), M (merge), S (strings), I "
            "(info),\n");
    fprintf(stdout,
            "  L (link order), O (extra OS processing required), G (group), T "
            "(TLS),\n");
    fprintf(stdout, "  C (compressed)\n");

    free(shdrs);
    free(strtab);
}
