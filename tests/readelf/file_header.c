#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "elf.h"
#include "elf_header.h"
#include "elf_ident.h"
#include "file.h"

Elf_Internal_Ehdr *get_elf_header(FILE *file)
{
    char *buf = (char *) read_file(file, sizeof(Elf64_Ehdr), 0);
    Elf_Internal_Ehdr *header =
        (Elf_Internal_Ehdr *) malloc(sizeof(Elf_Internal_Ehdr));
    if (!header) {
        perror("readelf");
        free(buf);
        exit(errno);
    }

    for (int i = 0; i < EI_NIDENT; i++)
        header->e_ident[i] = buf[i];

    if (buf[EI_CLASS] == ELFCLASS64) {
        Elf64_Ehdr *elf64 = (Elf64_Ehdr *) buf;

        header->e_type = elf64->e_type;
        header->e_machine = elf64->e_machine;
        header->e_version = elf64->e_version;
        header->e_entry = elf64->e_entry;
        header->e_phoff = elf64->e_phoff;
        header->e_shoff = elf64->e_shoff;
        header->e_flags = elf64->e_flags;
        header->e_ehsize = elf64->e_ehsize;
        header->e_phentsize = elf64->e_phentsize;
        header->e_phnum = elf64->e_phnum;
        header->e_shentsize = elf64->e_shentsize;
        header->e_shnum = elf64->e_shnum;
        header->e_shstrndx = elf64->e_shstrndx;
    } else {
        Elf32_Ehdr *elf32 = (Elf32_Ehdr *) buf;

        header->e_type = elf32->e_type;
        header->e_machine = elf32->e_machine;
        header->e_version = elf32->e_version;
        header->e_entry = elf32->e_entry;
        header->e_phoff = elf32->e_phoff;
        header->e_shoff = elf32->e_shoff;
        header->e_flags = elf32->e_flags;
        header->e_ehsize = elf32->e_ehsize;
        header->e_phentsize = elf32->e_phentsize;
        header->e_phnum = elf32->e_phnum;
        header->e_shentsize = elf32->e_shentsize;
        header->e_shnum = elf32->e_shnum;
        header->e_shstrndx = elf32->e_shstrndx;
    }

    free(buf);

    return header;
}

void print_magic(const unsigned char *e_ident)
{
    fprintf(stdout, "  Magic:  ");
    for (int i = 0; i < EI_NIDENT; i++) {
        printf(" %02x", e_ident[i]);
    }
    fprintf(stdout, "\n");
}

char *get_file_class(const unsigned char class)
{
    static char buf[32];

    switch (class) {
    case ELFCLASSNONE:
        return "none";
    case ELFCLASS32:
        return "ELF32";
    case ELFCLASS64:
        return "ELF64";
    default:
        snprintf(buf, sizeof(buf), "<unknown: %u>", class);
    }

    return buf;
}

char *get_data_encoding(const unsigned char encoding)
{
    static char buf[32];

    switch (encoding) {
    case ELFDATANONE:
        return "none";
    case ELFDATA2LSB:
        return "2's complement, little endian";
    case ELFDATA2MSB:
        return "2's complement, big endian";
    default:
        snprintf(buf, sizeof(buf), "<unknown: %x>", encoding);
    }

    return buf;
}

char *get_elf_version(const unsigned char version)
{
    static char buf[32];

    switch (version) {
    case EV_CURRENT:
        return "1 (current)";
    case EV_NONE:
        return "0";
    default:
        snprintf(buf, sizeof(buf), "%x <unknown>", version);
    }

    return buf;
}

char *get_osabi_name(const unsigned char osabi)
{
    static char buf[32];

    switch (osabi) {
    case ELFOSABI_NONE:
        return "UNIX - System V";
    case ELFOSABI_HPUX:
        return "UNIX - HP-UX";
    case ELFOSABI_NETBSD:
        return "UNIX - NetBSD";
    case ELFOSABI_GNU:
        return "UNIX - GNU";
    case ELFOSABI_SOLARIS:
        return "UNIX - Solaris";
    case ELFOSABI_AIX:
        return "UNIX - AIX";
    case ELFOSABI_IRIX:
        return "UNIX - IRIX";
    case ELFOSABI_FREEBSD:
        return "UNIX - FreeBSD";
    case ELFOSABI_TRU64:
        return "UNIX - TRU64";
    case ELFOSABI_MODESTO:
        return "Novell - Modesto";
    case ELFOSABI_OPENBSD:
        return "UNIX - OpenBSD";
    case ELFOSABI_OPENVMS:
        return "VMS - OpenVMS";
    case ELFOSABI_NSK:
        return "HP - Non-Stop Kernel";
    case ELFOSABI_AROS:
        return "AROS";
    case ELFOSABI_FENIXOS:
        return "FenixOS";
    case ELFOSABI_CLOUDABI:
        return "Nuxi - CloudABI";
    case ELFOSABI_OPENVOS:
        return "Stratus Technologies OpenVOS";
    default:
        if (osabi >= 64) {
            // TODO: machine dependent osabi
            return "";
        } else {
            snprintf(buf, sizeof(buf), "<unknown: %x>", osabi);
        }
    }

    return buf;
}

char *get_file_type(uint16_t e_type)
{
    static char buf[32];

    switch (e_type) {
    case ET_NONE:
        return "NONE (None)";
    case ET_REL:
        return "REL (Relocatable file)";
    case ET_EXEC:
        return "EXEC (Executable file)";
    case ET_DYN:
        return "DYN (Shared object file)";
    case ET_CORE:
        return "CORE (Core file)";
    default:
        if (e_type >= ET_LOOS && e_type <= ET_HIOS) {
            snprintf(buf, sizeof(buf), "OS Specific: (%x)", e_type);
        } else if (e_type >= ET_LOPROC && e_type <= ET_HIPROC) {
            snprintf(buf, sizeof(buf), "Processor Specific: (%x)", e_type);
        } else {
            snprintf(buf, sizeof(buf), "<unknown>: (%x)", e_type);
        }
    }

    return buf;
}

char *get_machine_name(uint16_t e_machine)
{
    static char buf[64];

    switch (e_machine) {
    case EM_NONE:
        return "None";
    case EM_M32:
        return "WE32100";
    case EM_SPARC:
        return "Sparc";
    case EM_386:
        return "Intel 80386";
    case EM_68K:
        return "MC68000";
    case EM_88K:
        return "MC88000";
    case EM_IAMCU:
        return "Intel MCU";
    case EM_860:
        return "Intel 80860";
    case EM_MIPS:
        return "MIPS R3000";
    case EM_S370:
        return "IBM System/370";
    case EM_MIPS_RS3_LE:
        return "MIPS R4000 big-endian";
    case EM_PARISC:
        return "HPPA";
    case EM_VPP500:
        return "Fujitsu VPP500";
    case EM_SPARC32PLUS:
        return "Sparc v8+";
    case EM_960:
        return "Intel 90860";
    case EM_PPC:
        return "PowerPC";
    case EM_PPC64:
        return "PowerPC64";
    case EM_S390:
        return "IBM S/390";
    case EM_SPU:
        return "SPU";
    case EM_V800:
        return "Renesas V850 (formerly Mitsubishi M32r)";
    case EM_FR20:
        return "Fujitsu FR20";
    case EM_RH32:
        return "TRW RH32";
    case EM_RCE:
        return "MCORE";
    case EM_ARM:
        return "ARM";
    case EM_ALPHA:
        return "Alpha";
    case EM_SH:
        return "Renesas / SuperH SH";
    case EM_SPARCV9:
        return "Sparc v9";
    case EM_TRICORE:
        return "Siemens Tricore";
    case EM_ARC:
        return "ARC";
    case EM_H8_300:
        return "Renesas H8/300";
    case EM_H8_300H:
        return "Renesas H8/300H";
    case EM_H8S:
        return "Renesas H8S";
    case EM_H8_500:
        return "Renesas H8/500";
    case EM_IA_64:
        return "Intel IA-64";
    case EM_MIPS_X:
        return "Stanford MIPS-X";
    // TODO
    case EM_LATTINCEMICO32:
        return "Lattice Micro32";
    case EM_SE_C17:
        return "Seiko Epson C17 Family";
    case EM_TI_C6000:
        return "Texas Instruments TMS320C6000 DSP family";
    case EM_TI_C2000:
        return "Texas Instruments TMS320C2000 DSP family";
    case EM_TI_C5500:
        return "Texas Instruments TMS320C55x DSP family";
    case EM_TI_PRU:
        return "TI PRU I/O processor";
    case EM_MMDSP_PLUS:
        return "STMicroelectronics 64bit VLIW Data Signal Processor";
    case EM_CYPRESS_M8C:
        return "Cypress M8C microprocessor";
    case EM_R32C:
        return "Renesas R32C series microprocessor";
    case EM_TRIMEDIA:
        return "NXP Semiconductors Trimedia architecture family";
    case EM_QDSP6:
        return "QUALCOMM DSP6 Processor";
    case EM_8051:
        return "Intel 8051 and variants";
    case EM_STXP7X:
        return "STMicroelectronics STxP7x family";
    case EM_NDS32:
        return "Andes Technology compact code size embedded RISC processor "
               "family";
    case EM_ECOG1X:
        return "Cyan Technology eCOG1X family";
    case EM_MAXQ30:
        return "Dakkas Semiconductor MAXQ30 Core microcontrollers";
    case EM_XIMO16:
        return "New Japan Radio (NJR) 16-bit DSP Processor";
    case EM_MANIK:
        return "M2000 Reconfigurable RISC Microprocessor";
    case EM_CRAYNV2:
        return "Cray Inc. NV2 vector architecture";
    case EM_RX:
        return "Renesas RX";
    case EM_METAG:
        return "Imagination Technologies Meta processor architecture";
    case EM_MCST_ELBRUS:
        return "MCST Elbrus general purpose hardware architecture";
    case EM_ECOG16:
        return "Cyan Technology eCOG16 family";
    case EM_CR16:
    case EM_MICROBLAZE:
        return "Xilinx MicroBlaze";
    case EM_ETPU:
        return "Freescale Extended Time Processing Unit";
    case EM_SLE9X:
        return "Infineon Technologies SLE9X core";
    case EM_L10M:
        return "Intel L10M";
    case EM_K10M:
        return "Intel K10M";
    case EM_AARCH64:
        return "AArch64";
    case EM_AVR32:
        return "Atmel Corporation 32-bit microprocessor family";
    case EM_STM8:
        return "STMicroelectronics STM8 8-bit microcontroller";
    case EM_TILE64:
        return "Tilera TILE64 multicore architecture family";
    case EM_TILEPRO:
        return "Tilera TILEPro multicore architectire family";
    case EM_CUDA:
        return "NVIDIA CUDA architecture";
    // TODO
    case EM_AMDGPU:
        return "AMD GPU";
    case EM_RISCV:
        return "RISC-V";
    default:
        snprintf(buf, sizeof(buf), "<unknown>: 0x%x", e_machine);
    }

    return buf;
}

void display_file_header(const Elf_Internal_Ehdr *header)
{
    if (check_elf_magic_num(header->e_ident)) {
        fprintf(stderr,
                "readelf: Error: Not an ELF file - it has wrong magic bytes at "
                "the starts\n");
        exit(1);
    }

    fprintf(stdout, "ELF Header:\n");
    print_magic(header->e_ident);
    fprintf(stdout, "  Class:                             %s\n",
            get_file_class(header->e_ident[EI_CLASS]));
    fprintf(stdout, "  Data:                              %s\n",
            get_data_encoding(header->e_ident[EI_DATA]));
    fprintf(stdout, "  Version:                           %s\n",
            get_elf_version(header->e_ident[EI_VERSION]));
    fprintf(stdout, "  OS/ABI:                            %s\n",
            get_osabi_name(header->e_ident[EI_OSABI]));
    fprintf(stdout, "  ABI Version:                       %x\n",
            header->e_ident[EI_ABIVERSION]);
    fprintf(stdout, "  Type:                              %s\n",
            get_file_type(header->e_type));
    fprintf(stdout, "  Machine:                           %s\n",
            get_machine_name(header->e_machine));
    fprintf(stdout, "  Version:                           0x%x\n",
            header->e_version);
    fprintf(stdout, "  Entry point address:               0x%llx\n",
            header->e_entry);
    fprintf(stdout,
            "  Start of program headers:          %llu (bytes into file)\n",
            header->e_phoff);
    fprintf(stdout,
            "  Start of section headers:          %llu (bytes into file)\n",
            header->e_shoff);
    // TODO: machine specific flags.
    fprintf(stdout, "  Flags:                             0x%x\n",
            header->e_flags);
    fprintf(stdout, "  Size of this header:               %d (bytes)\n",
            header->e_ehsize);
    fprintf(stdout, "  Size of program headers:           %d (bytes)\n",
            header->e_phentsize);
    fprintf(stdout, "  Number of program headers:         %d\n",
            header->e_phnum);
    fprintf(stdout, "  Size of section headers:           %d (bytes)\n",
            header->e_shentsize);
    fprintf(stdout, "  Number of section headers:         %d\n",
            header->e_shnum);
    fprintf(stdout, "  Section header string table index: %d\n",
            header->e_shstrndx);
}
